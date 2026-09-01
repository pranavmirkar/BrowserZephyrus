// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_workspace_image.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/format_macros.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/task/task_traits.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/data_decoder/public/cpp/decode_image.h"
#include "services/data_decoder/public/mojom/image_decoder.mojom.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace zephyrus {
namespace {

// Where the canonical PNGs live, inside the profile.
constexpr base::FilePath::CharType kImageDirName[] =
    FILE_PATH_LITERAL("Zephyrus Workspace Icons");

// What we store on disk. 128px covers a 22dip cell at any DPI anyone has, and
// costs a few kilobytes -- there is no reason to keep a 12-megapixel original
// just to draw a circle the size of a fingernail.
constexpr int kStoredSizePx = 128;

// Refuse to even READ anything larger than this. The decoder is sandboxed and
// has its own ceiling, but a 400MB file should not be pulled into memory to
// find that out -- and a file that size is a mistake or an attack, never a
// workspace icon.
constexpr int64_t kMaxSourceBytes = 24 * 1024 * 1024;

// Passed to the decoder as its own limit on the DECODED size, which is the
// number that actually matters: a small compressed file can expand enormously.
constexpr uint64_t kMaxDecodedBytes = 64 * 1024 * 1024;

// One shared decoder connection rather than an isolated one per call. Loading
// six workspace icons at startup should not spawn six utility processes; the
// service shuts itself down once it has been idle a while.
data_decoder::DataDecoder& SharedDecoder() {
  static base::NoDestructor<data_decoder::DataDecoder> decoder;
  return *decoder;
}

base::FilePath ImageDir(Profile* profile) {
  return profile->GetPath().Append(kImageDirName);
}

// Centre-crop to a square, then scale. Cropping BEFORE scaling is what keeps a
// wide photo from being squashed into the circle: the disc shows the middle of
// the picture at its true proportions, which is what every avatar picker does
// and what people expect from one.
SkBitmap CropAndScale(const SkBitmap& source, int size_px) {
  if (source.drawsNothing() || size_px <= 0) {
    return SkBitmap();
  }
  const int side = std::min(source.width(), source.height());
  const int left = (source.width() - side) / 2;
  const int top = (source.height() - side) / 2;

  // Crop the SOURCE with extractSubset, then resize the whole of that.
  //
  // NOT via Resize()'s `dest_subset` parameter, which is the trap here: it is a
  // rect in the DESTINATION image ("the rectangle in this destination image
  // that should actually be returned"), not a crop of the source. Passing a
  // source-space rect trips DCHECK(dest.contains(dest_subset)) in
  // image_operations.cc and aborts the browser -- for any photo larger than
  // size_px, which is every real photo.
  SkBitmap cropped;
  if (!source.extractSubset(&cropped, SkIRect::MakeXYWH(left, top, side, side))) {
    return SkBitmap();
  }
  return skia::ImageOperations::Resize(
      cropped, skia::ImageOperations::RESIZE_LANCZOS3, size_px, size_px);
}

// ---------------------------------------------------------------------------
// Worker-thread steps. All of these block, so none of them may run on the UI
// thread.

std::optional<std::vector<uint8_t>> ReadCapped(const base::FilePath& path) {
  std::optional<int64_t> size = base::GetFileSize(path);
  if (!size.has_value() || *size <= 0 || *size > kMaxSourceBytes) {
    return std::nullopt;
  }
  std::string bytes;
  if (!base::ReadFileToStringWithMaxSize(path, &bytes,
                                         static_cast<size_t>(*size))) {
    return std::nullopt;
  }
  return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

// Scales the decoded bitmap down and writes it as `name` in `dir`. Returns the
// basename on success, empty on failure.
std::string WriteCanonicalPng(const base::FilePath& dir,
                              const std::string& name,
                              const SkBitmap& decoded) {
  const SkBitmap square = CropAndScale(decoded, kStoredSizePx);
  if (square.drawsNothing()) {
    return std::string();
  }
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(square, /*discard_transparency=*/false);
  if (!png.has_value()) {
    return std::string();
  }
  if (!base::CreateDirectory(dir)) {
    return std::string();
  }
  // A torn write leaves an unreadable PNG, which the load path already treats
  // as "no image" rather than as an error -- so no temp-file dance is needed
  // for a file whose worst failure mode is a blank disc.
  if (!base::WriteFile(dir.AppendASCII(name), *png)) {
    return std::string();
  }
  return name;
}

// ---------------------------------------------------------------------------
// The pick flow.
//
// SelectFileDialog holds a RAW pointer to its listener and offers no ownership
// guarantee, so the listener has to outlive the dialog by construction. This
// object owns itself and destroys itself on exactly one of the three exits
// (chosen, cancelled, failed) -- which is why every path below ends in
// Finish().
class WorkspaceImagePicker : public ui::SelectFileDialog::Listener {
 public:
  WorkspaceImagePicker(Profile* profile,
                       int workspace_id,
                       base::OnceCallback<void(const std::string&)> on_done)
      : dir_(ImageDir(profile)),
        workspace_id_(workspace_id),
        on_done_(std::move(on_done)) {}

  WorkspaceImagePicker(const WorkspaceImagePicker&) = delete;
  WorkspaceImagePicker& operator=(const WorkspaceImagePicker&) = delete;

  // Public only so DeleteSoon() can reach it -- see Finish(). Nothing else may
  // destroy this object; it owns itself.
  ~WorkspaceImagePicker() override = default;

  void Show(gfx::NativeWindow parent) {
    dialog_ = ui::SelectFileDialog::Create(
        this, std::make_unique<ChromeSelectFilePolicy>(nullptr));
    if (!dialog_) {
      Finish(std::string());
      return;
    }
    // An explicit extension list rather than everything the MIME database calls
    // an image: offering a file the decoder will certainly reject produces a
    // silent no-op after the user has done the work of finding it.
    //
    // This mirrors blink's kSupportedImageTypes (mime_util.cc), which is what
    // ImageCodec::kDefault dispatches to -- minus xbm, which nobody has a photo
    // in. AVIF is included because enable_dav1d_decoder follows use_blink and
    // is therefore on in this build; phones have started producing it.
    ui::SelectFileDialog::FileTypeInfo types;
    types.extensions.push_back({FILE_PATH_LITERAL("png"),
                                FILE_PATH_LITERAL("jpg"),
                                FILE_PATH_LITERAL("jpeg"),
                                FILE_PATH_LITERAL("webp"),
                                FILE_PATH_LITERAL("avif"),
                                FILE_PATH_LITERAL("gif"),
                                FILE_PATH_LITERAL("bmp"),
                                FILE_PATH_LITERAL("ico")});
    types.include_all_files = false;
    dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE,
                        u"Choose a workspace photo", base::FilePath(), &types,
                        0, base::FilePath::StringType(), parent);
  }

 private:
  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ReadCapped, file.path()),
        base::BindOnce(&WorkspaceImagePicker::OnRead,
                       weak_factory_.GetWeakPtr()));
  }

  void FileSelectionCanceled() override { Finish(std::string()); }

  void OnRead(std::optional<std::vector<uint8_t>> bytes) {
    if (!bytes.has_value()) {
      Finish(std::string());
      return;
    }
    // kDefault sniffs the format rather than trusting the extension, which is
    // the right way round: the extension came from the filename.
    data_decoder::DecodeImage(
        &SharedDecoder(), *bytes, data_decoder::mojom::ImageCodec::kDefault,
        /*shrink_to_fit=*/true, kMaxDecodedBytes,
        gfx::Size(kStoredSizePx, kStoredSizePx),
        base::BindOnce(&WorkspaceImagePicker::OnDecoded,
                       weak_factory_.GetWeakPtr()));
  }

  void OnDecoded(const SkBitmap& decoded) {
    if (decoded.drawsNothing()) {
      Finish(std::string());
      return;
    }
    // The timestamp makes replacing an icon produce a NEW filename. Reusing
    // "<id>.png" would mean the old bytes stay cached in memory and on disk
    // under a name the code has no reason to re-read -- the classic "I changed
    // it and nothing happened" bug.
    const std::string name = base::StringPrintf(
        "ws-%d-%" PRId64 ".png", workspace_id_,
        base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&WriteCanonicalPng, dir_, name, decoded),
        base::BindOnce(&WorkspaceImagePicker::Finish,
                       weak_factory_.GetWeakPtr()));
  }

  void Finish(const std::string& name) {
    // Exactly once. ListenerDestroyed() below stops the dialog calling back,
    // but the object stays alive until the deletion task runs, so a second
    // entry here would queue a second delete of the same pointer.
    if (finished_) {
      return;
    }
    finished_ = true;

    if (on_done_) {
      std::move(on_done_).Run(name);
    }
    if (dialog_) {
      dialog_->ListenerDestroyed();
    }

    // NOT `delete this`.
    //
    // Finish() is reachable from inside a SelectFileDialog callback --
    // FileSelectionCanceled() calls it directly. Destroying this object there
    // releases our reference to the dialog, and the dialog is refcounted with
    // no documented guarantee that it holds one on itself across the callback.
    // Dropping the last reference from inside its own stack frame is a
    // use-after-free that would only ever show up as a cancel-the-dialog crash.
    //
    // Deleting on a fresh task means the dialog's frame has unwound first.
    // `dialog_` is deliberately NOT reset above -- our reference is what keeps
    // it alive until then.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

  base::FilePath dir_;
  int workspace_id_;
  base::OnceCallback<void(const std::string&)> on_done_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
  bool finished_ = false;
  base::WeakPtrFactory<WorkspaceImagePicker> weak_factory_{this};
};

// Turns a decoded bitmap into an ImageSkia sized for `size_dip`.
//
// The rep is registered at scale 2.0 rather than 1.0 so a HiDPI screen draws it
// natively and a 1x screen filters DOWN, which is the direction that survives.
// Registering a 1x rep and letting HiDPI scale UP is the version that looks
// blurry on exactly the screens people notice on.
gfx::ImageSkia ToImageSkia(const SkBitmap& decoded, int size_dip) {
  const SkBitmap scaled = CropAndScale(decoded, size_dip * 2);
  if (scaled.drawsNothing()) {
    return gfx::ImageSkia();
  }
  gfx::ImageSkia image;
  image.AddRepresentation(gfx::ImageSkiaRep(scaled, 2.0f));
  return image;
}

void OnLoadDecoded(int size_dip,
                   base::OnceCallback<void(const gfx::ImageSkia&)> on_done,
                   const SkBitmap& decoded) {
  std::move(on_done).Run(decoded.drawsNothing()
                             ? gfx::ImageSkia()
                             : ToImageSkia(decoded, size_dip));
}

void OnLoadRead(int size_dip,
                base::OnceCallback<void(const gfx::ImageSkia&)> on_done,
                std::optional<std::vector<uint8_t>> bytes) {
  if (!bytes.has_value()) {
    std::move(on_done).Run(gfx::ImageSkia());
    return;
  }
  // kDefault, NOT kPng -- even though we wrote this file and know it is a PNG.
  //
  // ImageCodec::kPng is handled ONLY inside an IS_CHROMEOS block in
  // image_decoder_impl.cc. Everywhere else it matches no branch at all and the
  // decode returns a null bitmap, silently. Passing it here meant every stored
  // photo loaded as "nothing there" on Windows -- picking one appeared to work
  // and the icon simply never showed up.
  //
  // kDefault sniffs the format, which is also the safer choice: it does not
  // trust our own assumption about what is in the file.
  data_decoder::DecodeImage(
      &SharedDecoder(), *bytes, data_decoder::mojom::ImageCodec::kDefault,
      /*shrink_to_fit=*/true, kMaxDecodedBytes, gfx::Size(),
      base::BindOnce(&OnLoadDecoded, size_dip, std::move(on_done)));
}

bool AllDigits(std::string_view s) {
  return !s.empty() && std::ranges::all_of(s, [](char c) {
    return c >= '0' && c <= '9';
  });
}

}  // namespace

bool IsValidWorkspaceImageName(const std::string& name) {
  // Shape: ws-<digits>-<digits>.png, and nothing else. This is deliberately
  // stricter than "contains no path separators" -- the name is about to be
  // joined onto a directory path, and a whitelist of the shape we generate
  // cannot be talked into naming a file we did not write, whereas a blacklist
  // is only ever as good as the last bypass someone thought of.
  static constexpr std::string_view kPrefix = "ws-";
  static constexpr std::string_view kSuffix = ".png";
  // Room for the prefix, the suffix, two digit runs and the separator between
  // them; and a ceiling, so a pathological pref value is rejected on sight.
  if (name.size() < kPrefix.size() + kSuffix.size() + 3 || name.size() > 64) {
    return false;
  }
  if (!name.starts_with(kPrefix) || !name.ends_with(kSuffix)) {
    return false;
  }
  const std::string_view middle = std::string_view(name).substr(
      kPrefix.size(), name.size() - kPrefix.size() - kSuffix.size());
  const size_t dash = middle.find('-');
  if (dash == std::string_view::npos) {
    return false;
  }
  return AllDigits(middle.substr(0, dash)) &&
         AllDigits(middle.substr(dash + 1));
}

void PickWorkspaceImage(Profile* profile,
                        int workspace_id,
                        gfx::NativeWindow parent,
                        base::OnceCallback<void(const std::string&)> on_done) {
  if (!profile) {
    std::move(on_done).Run(std::string());
    return;
  }
  // Owns itself; deletes itself in Finish().
  (new WorkspaceImagePicker(profile, workspace_id, std::move(on_done)))
      ->Show(parent);
}

void LoadWorkspaceImage(
    Profile* profile,
    const std::string& name,
    int size_dip,
    base::OnceCallback<void(const gfx::ImageSkia&)> on_done) {
  if (!profile || !IsValidWorkspaceImageName(name) || size_dip <= 0) {
    std::move(on_done).Run(gfx::ImageSkia());
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadCapped, ImageDir(profile).AppendASCII(name)),
      base::BindOnce(&OnLoadRead, size_dip, std::move(on_done)));
}

void DeleteWorkspaceImage(Profile* profile, const std::string& name) {
  if (!profile || !IsValidWorkspaceImageName(name)) {
    return;
  }
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::GetDeleteFileCallback(ImageDir(profile).AppendASCII(name)));
}

}  // namespace zephyrus
