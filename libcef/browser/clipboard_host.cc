// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "cef/libcef/browser/clipboard_host.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/common/app_manager.h"
#include "cef/libcef/common/net/url_util.h"
#include "content/browser/file_system/browser_file_system_helper.h"
#include "content/browser/renderer_host/data_transfer_util.h"
#include "content/browser/security/cpsp/child_process_security_policy_impl.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/disallow_activation_reason.h"
#include "content/public/browser/render_process_host.h"
#include "crypto/hmac.h"
#include "third_party/abseil-cpp/absl/numeric/int128.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_constants.h"
#include "ui/base/clipboard/clipboard_monitor.h"
#include "ui/base/clipboard/file_info.h"
#include "ui/base/data_transfer_policy/data_transfer_endpoint.h"
#include "ui/gfx/codec/png_codec.h"

namespace {

// Tracks a process-wide sequence number for clipboard contents observed via
// handler-enabled frames. The token changes whenever the native clipboard
// changes or when CefNotifyClipboardChanged() is called. UI thread only.
class CefClipboardChangeTracker final : public ui::ClipboardObserver {
 public:
  static CefClipboardChangeTracker* Get() {
    static base::NoDestructor<CefClipboardChangeTracker> tracker;
    return tracker.get();
  }

  const ui::ClipboardSequenceNumberToken& token() const { return token_; }

  void OnClipboardDataChanged() override {
    token_ = ui::ClipboardSequenceNumberToken();
  }

 private:
  friend class base::NoDestructor<CefClipboardChangeTracker>;

  CefClipboardChangeTracker() {
    ui::ClipboardMonitor::GetInstance()->AddObserver(this);
  }

  ui::ClipboardSequenceNumberToken token_;
};

class CefClipboardReadCallbackImpl final : public CefClipboardReadCallback {
 public:
  CefClipboardReadCallbackImpl(
      base::OnceCallback<void(CefRefPtr<CefClipboardData>)> callback,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner)
      : callback_(std::move(callback)), task_runner_(std::move(task_runner)) {}

  ~CefClipboardReadCallbackImpl() override { Continue(nullptr); }

  void Continue(CefRefPtr<CefClipboardData> data) override {
    base::OnceCallback<void(CefRefPtr<CefClipboardData>)> callback;
    {
      base::AutoLock lock_scope(lock_);
      if (callback_.is_null()) {
        return;
      }
      callback = std::move(callback_);
    }

    if (task_runner_->RunsTasksInCurrentSequence()) {
      std::move(callback).Run(data ? data->Clone() : nullptr);
    } else {
      task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback), data ? data->Clone() : nullptr));
    }
  }

 private:
  base::Lock lock_;
  base::OnceCallback<void(CefRefPtr<CefClipboardData>)> callback_;
  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  IMPLEMENT_REFCOUNTING(CefClipboardReadCallbackImpl);
};

cef_clipboard_buffer_t ToCefBuffer(ui::ClipboardBuffer buffer) {
  return buffer == ui::ClipboardBuffer::kSelection
             ? CEF_CLIPBOARD_BUFFER_SELECTION
             : CEF_CLIPBOARD_BUFFER_STANDARD;
}

std::vector<std::u16string> GetMimeTypes(CefRefPtr<CefClipboardData> data) {
  std::vector<std::u16string> types;
  if (!data) {
    return types;
  }

  const uint32_t formats = data->GetAvailableFormats();
  if (formats & CEF_CLIPBOARD_FORMAT_FILES) {
    // Do not expose paths through text/plain when files are available.
    types.push_back(ui::kMimeTypeUriList16);
    return types;
  }

  if (formats & CEF_CLIPBOARD_FORMAT_TEXT) {
    types.push_back(ui::kMimeTypePlainText16);
  }
  if (formats & CEF_CLIPBOARD_FORMAT_HTML) {
    types.push_back(ui::kMimeTypeHtml16);
  }
  if (formats & CEF_CLIPBOARD_FORMAT_RTF) {
    types.push_back(ui::kMimeTypeRtf16);
  }
  if (formats & CEF_CLIPBOARD_FORMAT_SVG) {
    types.push_back(ui::kMimeTypeSvg16);
  }
  if (formats & CEF_CLIPBOARD_FORMAT_IMAGE) {
    types.push_back(ui::kMimeTypePng16);
  }
  if (formats & CEF_CLIPBOARD_FORMAT_BOOKMARK) {
    types.push_back(ui::kMimeTypeMozillaUrl16);
  }

  std::map<CefString, CefString> custom_data;
  data->GetCustomData(custom_data);
  for (const auto& [type, value] : custom_data) {
    types.push_back(type.ToString16());
  }
  return types;
}

std::vector<std::u16string> FilterClipboardChangeTypes(
    std::vector<std::u16string> types) {
  std::vector<std::u16string> filtered;
  for (const auto& type : types) {
    if (type == ui::kMimeTypePng16 || type == ui::kMimeTypeHtml16 ||
        type == ui::kMimeTypePlainText16) {
      filtered.push_back(type);
    }
  }
  return filtered;
}

void NotifyClipboardChangedOnUI() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Ensure the tracker observes the monitor before the notification fires.
  CefClipboardChangeTracker::Get();
  ui::ClipboardMonitor::GetInstance()->NotifyClipboardDataChanged();
}

}  // namespace

CefClipboardHost::CefClipboardHost(
    content::RenderFrameHost& render_frame_host,
    mojo::PendingReceiver<blink::mojom::ClipboardHost> receiver,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefClipboardHandler> handler)
    : content::DocumentService<blink::mojom::ClipboardHost>(
          render_frame_host,
          std::move(receiver)),
      render_frame_host_(render_frame_host),
      browser_(std::move(browser)),
      frame_(std::move(frame)),
      handler_(std::move(handler)),
      fallback_(
          std::make_unique<content::ClipboardHostImpl>(render_frame_host)),
      pending_data_(std::make_unique<CefClipboardDataImpl>()) {
  CefClipboardChangeTracker::Get();
}

CefClipboardHost::~CefClipboardHost() {
  if (observing_clipboard_) {
    ui::ClipboardMonitor::GetInstance()->RemoveObserver(this);
  }
}

void CefClipboardHost::NotifyClipboardChanged() {
  if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    NotifyClipboardChangedOnUI();
  } else {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&NotifyClipboardChangedOnUI));
  }
}

bool CefClipboardHost::IsPasteAllowed() const {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    return false;
  }
  return CefAppManager::Get()
      ->GetContentClient()
      ->browser()
      ->IsClipboardPasteAllowed(&*render_frame_host_);
}

uint32_t CefClipboardHost::GetFormatsHint(
    blink::mojom::ClipboardFormat format) const {
  switch (format) {
    case blink::mojom::ClipboardFormat::kPlaintext:
      return CEF_CLIPBOARD_FORMAT_TEXT;
    case blink::mojom::ClipboardFormat::kHtml:
      return CEF_CLIPBOARD_FORMAT_HTML;
    case blink::mojom::ClipboardFormat::kSmartPaste:
      return CEF_CLIPBOARD_FORMAT_TEXT;
    case blink::mojom::ClipboardFormat::kBookmark:
      return CEF_CLIPBOARD_FORMAT_BOOKMARK;
  }
  return CEF_CLIPBOARD_FORMAT_NONE;
}

void CefClipboardHost::RequestData(ui::ClipboardBuffer buffer,
                                   uint32_t formats_hint,
                                   ReadDataCallback callback) {
  auto read_callback =
      CefRefPtr<CefClipboardReadCallbackImpl>(new CefClipboardReadCallbackImpl(
          std::move(callback),
          base::SingleThreadTaskRunner::GetCurrentDefault()));
  if (!handler_->OnClipboardRead(browser_, frame_, ToCefBuffer(buffer),
                                 formats_hint, read_callback)) {
    read_callback->Continue(nullptr);
  }
}

absl::uint128 CefClipboardHost::GetSequenceNumberImpl() const {
  auto* storage_partition = static_cast<content::StoragePartitionImpl*>(
      render_frame_host_->GetProcess()->GetStoragePartition());
  const base::UnguessableToken& salt =
      storage_partition->GetPartitionUUIDPerStorageKey(
          render_frame_host_->GetStorageKey());
  const std::array result = crypto::hmac::SignSha256(
      CefClipboardChangeTracker::Get()->token().value().AsBytes(),
      base::as_byte_span(
          base::StrCat({"clipboard-change-id-", salt.ToString()})));
  const base::span result_span(result);
  return absl::MakeUint128(
      base::U64FromLittleEndian(result_span.first<8>()),
      base::U64FromLittleEndian(result_span.subspan<8, 8>()));
}

void CefClipboardHost::GetSequenceNumber(ui::ClipboardBuffer buffer,
                                         GetSequenceNumberCallback callback) {
  // Matches ClipboardHostImpl: the number changes whenever the clipboard does,
  // so answering a context that cannot read tells it when a copy happened.
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    std::move(callback).Run(absl::uint128());
    return;
  }
  std::move(callback).Run(GetSequenceNumberImpl());
}

void CefClipboardHost::IsFormatAvailable(blink::mojom::ClipboardFormat format,
                                         ui::ClipboardBuffer buffer,
                                         IsFormatAvailableCallback callback) {
  if (!IsPasteAllowed()) {
    std::move(callback).Run(false);
    return;
  }
  RequestData(
      buffer, GetFormatsHint(format),
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self,
             blink::mojom::ClipboardFormat format, ui::ClipboardBuffer buffer,
             IsFormatAvailableCallback callback,
             CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->IsFormatAvailable(format, buffer,
                                                 std::move(callback));
              return;
            }
            const uint32_t formats = data->GetAvailableFormats();
            bool available = false;
            switch (format) {
              case blink::mojom::ClipboardFormat::kPlaintext:
                available = (formats & CEF_CLIPBOARD_FORMAT_TEXT) != 0;
                break;
              case blink::mojom::ClipboardFormat::kHtml:
                available = (formats & CEF_CLIPBOARD_FORMAT_HTML) != 0;
                break;
              case blink::mojom::ClipboardFormat::kSmartPaste:
                available = false;
                break;
              case blink::mojom::ClipboardFormat::kBookmark:
                available = (formats & CEF_CLIPBOARD_FORMAT_BOOKMARK) != 0;
                break;
            }
            std::move(callback).Run(available);
          },
          weak_ptr_factory_.GetWeakPtr(), format, buffer, std::move(callback)));
}

void CefClipboardHost::ReadAvailableTypes(ui::ClipboardBuffer buffer,
                                          ReadAvailableTypesCallback callback) {
  if (!IsPasteAllowed()) {
    std::move(callback).Run({});
    return;
  }
  RequestData(
      buffer, 0,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             ReadAvailableTypesCallback callback,
             CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadAvailableTypes(buffer, std::move(callback));
              return;
            }
            std::move(callback).Run(GetMimeTypes(std::move(data)));
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, std::move(callback)));
}

void CefClipboardHost::ReadText(ui::ClipboardBuffer buffer,
                                ReadTextCallback callback) {
  if (!IsPasteAllowed()) {
    fallback_->ReadText(buffer, std::move(callback));
    return;
  }
  RequestData(
      buffer, CEF_CLIPBOARD_FORMAT_TEXT,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             ReadTextCallback callback, CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadText(buffer, std::move(callback));
              return;
            }
            std::move(callback).Run(
                (data->GetAvailableFormats() & CEF_CLIPBOARD_FORMAT_TEXT)
                    ? static_cast<std::u16string>(data->GetText())
                    : std::u16string());
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, std::move(callback)));
}

void CefClipboardHost::ReadHtml(ui::ClipboardBuffer buffer,
                                ReadHtmlCallback callback) {
  if (!IsPasteAllowed()) {
    fallback_->ReadHtml(buffer, std::move(callback));
    return;
  }
  RequestData(
      buffer, CEF_CLIPBOARD_FORMAT_HTML,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             ReadHtmlCallback callback, CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadHtml(buffer, std::move(callback));
              return;
            }
            std::u16string markup;
            GURL source_url;
            if (data->GetAvailableFormats() & CEF_CLIPBOARD_FORMAT_HTML) {
              markup = static_cast<std::u16string>(data->GetHtml());
              source_url = url_util::MakeGURL(data->GetHtmlSourceUrl(), false);
            }
            const uint32_t length = static_cast<uint32_t>(markup.size());
            std::move(callback).Run(std::move(markup), source_url, 0, length);
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, std::move(callback)));
}

void CefClipboardHost::ReadSvg(ui::ClipboardBuffer buffer,
                               ReadSvgCallback callback) {
  if (!IsPasteAllowed()) {
    fallback_->ReadSvg(buffer, std::move(callback));
    return;
  }
  RequestData(
      buffer, CEF_CLIPBOARD_FORMAT_SVG,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             ReadSvgCallback callback, CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadSvg(buffer, std::move(callback));
              return;
            }
            std::move(callback).Run(
                (data->GetAvailableFormats() & CEF_CLIPBOARD_FORMAT_SVG)
                    ? static_cast<std::u16string>(data->GetSvg())
                    : std::u16string());
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, std::move(callback)));
}

void CefClipboardHost::ReadRtf(ui::ClipboardBuffer buffer,
                               ReadRtfCallback callback) {
  if (!IsPasteAllowed()) {
    fallback_->ReadRtf(buffer, std::move(callback));
    return;
  }
  RequestData(
      buffer, CEF_CLIPBOARD_FORMAT_RTF,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             ReadRtfCallback callback, CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadRtf(buffer, std::move(callback));
              return;
            }
            std::string rtf;
            if (data->GetAvailableFormats() & CEF_CLIPBOARD_FORMAT_RTF) {
              rtf = base::UTF16ToUTF8(
                  static_cast<std::u16string>(data->GetRtf()));
            }
            std::move(callback).Run(std::move(rtf));
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, std::move(callback)));
}

void CefClipboardHost::ReadPng(ui::ClipboardBuffer buffer,
                               ReadPngCallback callback) {
  if (!IsPasteAllowed()) {
    fallback_->ReadPng(buffer, std::move(callback));
    return;
  }
  RequestData(
      buffer, CEF_CLIPBOARD_FORMAT_IMAGE,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             ReadPngCallback callback, CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadPng(buffer, std::move(callback));
              return;
            }
            std::vector<uint8_t> png;
            if (data->GetAvailableFormats() & CEF_CLIPBOARD_FORMAT_IMAGE) {
              auto image = data->GetImage();
              if (image && image->IsValid()) {
                png.resize(image->GetSize());
                const size_t read = image->GetData(png.data(), png.size(), 0);
                png.resize(read);
              }
            }
            std::move(callback).Run(mojo_base::BigBuffer(std::move(png)));
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, std::move(callback)));
}

void CefClipboardHost::ReadFiles(ui::ClipboardBuffer buffer,
                                 ReadFilesCallback callback) {
  if (!IsPasteAllowed()) {
    fallback_->ReadFiles(buffer, std::move(callback));
    return;
  }
  RequestData(
      buffer, CEF_CLIPBOARD_FORMAT_FILES,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             ReadFilesCallback callback, CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadFiles(buffer, std::move(callback));
              return;
            }

            auto result = blink::mojom::ClipboardFiles::New();
            if (!(data->GetAvailableFormats() & CEF_CLIPBOARD_FORMAT_FILES)) {
              std::move(callback).Run(std::move(result));
              return;
            }

            std::vector<CefString> paths;
            std::vector<CefString> display_names;
            data->GetFiles(paths, display_names);
            if (paths.size() != display_names.size()) {
              std::move(callback).Run(std::move(result));
              return;
            }

            std::vector<ui::FileInfo> filenames;
            filenames.reserve(paths.size());
            for (size_t i = 0; i < paths.size(); ++i) {
              filenames.emplace_back(
                  base::FilePath::FromUTF8Unsafe(paths[i].ToString()),
                  base::FilePath::FromUTF8Unsafe(display_names[i].ToString()));
            }

            auto* storage_partition =
                static_cast<content::StoragePartitionImpl*>(
                    self->render_frame_host_->GetProcess()
                        ->GetStoragePartition());
            const auto child_id =
                self->render_frame_host_->GetProcess()->GetID();
            result->file_system_id =
                content::PrepareDataTransferFilenamesForChildProcess(
                    filenames,
                    content::ChildProcessSecurityPolicyImpl::GetInstance(),
                    child_id, storage_partition->GetFileSystemContext());
            auto files = content::FileInfosToDataTransferFiles(
                filenames, storage_partition->GetFileSystemAccessManager(),
                child_id.GetUnsafeValue());
            std::move(files.begin(), files.end(),
                      std::back_inserter(result->files));
            std::move(callback).Run(std::move(result));
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, std::move(callback)));
}

void CefClipboardHost::ReadDataTransferCustomData(
    ui::ClipboardBuffer buffer,
    const std::u16string& type,
    ReadDataTransferCustomDataCallback callback) {
  if (!IsPasteAllowed()) {
    fallback_->ReadDataTransferCustomData(buffer, type, std::move(callback));
    return;
  }
  RequestData(
      buffer, CEF_CLIPBOARD_FORMAT_CUSTOM_DATA,
      base::BindOnce(
          [](base::WeakPtr<CefClipboardHost> self, ui::ClipboardBuffer buffer,
             std::u16string type, ReadDataTransferCustomDataCallback callback,
             CefRefPtr<CefClipboardData> data) {
            if (!self) {
              return;
            }
            if (!data) {
              self->fallback_->ReadDataTransferCustomData(buffer, type,
                                                          std::move(callback));
              return;
            }
            std::u16string value;
            if (data->GetAvailableFormats() &
                CEF_CLIPBOARD_FORMAT_CUSTOM_DATA) {
              std::map<CefString, CefString> custom_data;
              data->GetCustomData(custom_data);
              auto iter = custom_data.find(CefString(type));
              if (iter != custom_data.end()) {
                value = static_cast<std::u16string>(iter->second);
              }
            }
            std::move(callback).Run(std::move(value));
          },
          weak_ptr_factory_.GetWeakPtr(), buffer, type, std::move(callback)));
}

void CefClipboardHost::ReadAvailableCustomAndStandardFormats(
    ReadAvailableCustomAndStandardFormatsCallback callback) {
  if (!IsPasteAllowed()) {
    std::move(callback).Run({});
    return;
  }
  RequestData(ui::ClipboardBuffer::kCopyPaste, 0,
              base::BindOnce(
                  [](base::WeakPtr<CefClipboardHost> self,
                     ReadAvailableCustomAndStandardFormatsCallback callback,
                     CefRefPtr<CefClipboardData> data) {
                    if (!self) {
                      return;
                    }
                    if (!data) {
                      self->fallback_->ReadAvailableCustomAndStandardFormats(
                          std::move(callback));
                      return;
                    }
                    std::move(callback).Run(GetMimeTypes(std::move(data)));
                  },
                  weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CefClipboardHost::ReadUnsanitizedCustomFormat(
    const std::u16string& format,
    ReadUnsanitizedCustomFormatCallback callback) {
  std::move(callback).Run(mojo_base::BigBuffer());
}

void CefClipboardHost::WriteUnsanitizedCustomFormat(
    const std::u16string& format,
    mojo_base::BigBuffer data) {
  if (!render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    pending_unsanitized_formats_.emplace_back(format, std::move(data));
  }
}

void CefClipboardHost::WriteText(const std::u16string& text) {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    return;
  }
  pending_data_->SetText(text);
}

void CefClipboardHost::WriteHtml(const std::u16string& markup,
                                 const GURL& url) {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    return;
  }
  pending_data_->SetHtml(markup, url.spec());
}

void CefClipboardHost::WriteSvg(const std::u16string& markup) {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    return;
  }
  pending_data_->SetSvg(markup);
}

void CefClipboardHost::WriteSmartPasteMarker() {
  if (!render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    pending_smart_paste_ = true;
  }
}

void CefClipboardHost::WriteDataTransferCustomData(
    const base::flat_map<std::u16string, std::u16string>& data) {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    return;
  }
  std::map<CefString, CefString> custom_data;
  for (const auto& [key, value] : data) {
    custom_data.emplace(CefString(key), CefString(value));
  }
  pending_data_->SetCustomData(custom_data);
}

void CefClipboardHost::WriteBookmark(const std::string& url,
                                     const std::u16string& title) {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    return;
  }
  pending_data_->SetBookmark(url, title);
}

void CefClipboardHost::WriteImage(const SkBitmap& unsafe_bitmap) {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    return;
  }
  auto encoded = gfx::PNGCodec::FastEncodeBGRASkBitmap(
      unsafe_bitmap, /*discard_transparency=*/false);
  if (!encoded) {
    return;
  }
  pending_data_->SetImage(
      CefBinaryValue::Create(encoded->data(), encoded->size()));
}

void CefClipboardHost::WritePendingDataToNativeClipboard() {
  const uint32_t formats = pending_data_->GetAvailableFormats();
  if (formats & CEF_CLIPBOARD_FORMAT_TEXT) {
    fallback_->WriteText(static_cast<std::u16string>(pending_data_->GetText()));
  }
  if (formats & CEF_CLIPBOARD_FORMAT_HTML) {
    fallback_->WriteHtml(
        static_cast<std::u16string>(pending_data_->GetHtml()),
        url_util::MakeGURL(pending_data_->GetHtmlSourceUrl(), false));
  }
  if (formats & CEF_CLIPBOARD_FORMAT_SVG) {
    fallback_->WriteSvg(static_cast<std::u16string>(pending_data_->GetSvg()));
  }
  if (formats & CEF_CLIPBOARD_FORMAT_CUSTOM_DATA) {
    std::map<CefString, CefString> cef_data;
    pending_data_->GetCustomData(cef_data);
    base::flat_map<std::u16string, std::u16string> custom_data;
    for (const auto& [key, value] : cef_data) {
      custom_data.emplace(static_cast<std::u16string>(key),
                          static_cast<std::u16string>(value));
    }
    fallback_->WriteDataTransferCustomData(custom_data);
  }
  if (formats & CEF_CLIPBOARD_FORMAT_BOOKMARK) {
    fallback_->WriteBookmark(pending_data_->GetBookmarkUrl().ToString(),
                             pending_data_->GetBookmarkTitle());
  }
  if (formats & CEF_CLIPBOARD_FORMAT_IMAGE) {
    auto encoded = pending_data_->GetImage();
    if (encoded && encoded->IsValid()) {
      std::vector<uint8_t> bytes(encoded->GetSize());
      const size_t read = encoded->GetData(bytes.data(), bytes.size(), 0);
      bytes.resize(read);
      const SkBitmap bitmap = gfx::PNGCodec::Decode(bytes);
      if (!bitmap.isNull()) {
        fallback_->WriteImage(bitmap);
      }
    }
  }
  if (pending_smart_paste_) {
    fallback_->WriteSmartPasteMarker();
  }
  for (auto& [format, data] : pending_unsanitized_formats_) {
    fallback_->WriteUnsanitizedCustomFormat(format, std::move(data));
  }
  fallback_->CommitWrite();
}

void CefClipboardHost::CommitWrite() {
  if (render_frame_host_->IsInactiveAndDisallowActivation(
          content::DisallowActivationReasonId::kClipboard)) {
    pending_data_ = std::make_unique<CefClipboardDataImpl>();
    pending_unsanitized_formats_.clear();
    pending_smart_paste_ = false;
    return;
  }

  CefRefPtr<CefClipboardDataImpl> data_copy(
      new CefClipboardDataImpl(*pending_data_));
  data_copy->SetReadOnly(true);
  if (!handler_->OnClipboardWrite(browser_, frame_,
                                  CEF_CLIPBOARD_BUFFER_STANDARD, data_copy)) {
    WritePendingDataToNativeClipboard();
  } else {
    NotifyClipboardChanged();
  }
  pending_data_ = std::make_unique<CefClipboardDataImpl>();
  pending_unsanitized_formats_.clear();
  pending_smart_paste_ = false;
}

#if BUILDFLAG(IS_MAC)
void CefClipboardHost::WriteStringToFindPboard(const std::u16string& text) {
  fallback_->WriteStringToFindPboard(text);
}

void CefClipboardHost::GetPlatformPermissionState(
    GetPlatformPermissionStateCallback callback) {
  fallback_->GetPlatformPermissionState(std::move(callback));
}
#endif

void CefClipboardHost::RegisterClipboardListener(
    mojo::PendingRemote<blink::mojom::ClipboardListener> listener) {
  if (!render_frame_host_->IsActive()) {
    return;
  }
  clipboard_listener_.reset();
  clipboard_listener_.Bind(std::move(listener));
  clipboard_listener_.set_disconnect_handler(base::BindOnce(
      [](base::WeakPtr<CefClipboardHost> self) {
        if (self && self->observing_clipboard_) {
          ui::ClipboardMonitor::GetInstance()->RemoveObserver(self.get());
          self->observing_clipboard_ = false;
        }
      },
      weak_ptr_factory_.GetWeakPtr()));
  if (!observing_clipboard_) {
    ui::ClipboardMonitor::GetInstance()->AddObserver(this);
    observing_clipboard_ = true;
  }
}

void CefClipboardHost::OnClipboardDataChanged() {
  if (!clipboard_listener_.is_bound() || !render_frame_host_->IsActive()) {
    return;
  }
  RequestData(ui::ClipboardBuffer::kCopyPaste,
              CEF_CLIPBOARD_FORMAT_TEXT | CEF_CLIPBOARD_FORMAT_HTML |
                  CEF_CLIPBOARD_FORMAT_IMAGE,
              base::BindOnce(&CefClipboardHost::NotifyListenerWithData,
                             weak_ptr_factory_.GetWeakPtr()));
}

void CefClipboardHost::NotifyListenerWithData(
    CefRefPtr<CefClipboardData> data) {
  if (!data) {
    // The handler declined; report the native clipboard types instead.
    ui::Clipboard::GetForCurrentThread()->ReadAvailableTypes(
        ui::ClipboardBuffer::kCopyPaste, std::nullopt,
        base::BindOnce(&CefClipboardHost::NotifyListenerWithTypes,
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  NotifyListenerWithTypes(GetMimeTypes(std::move(data)));
}

void CefClipboardHost::NotifyListenerWithTypes(
    std::vector<std::u16string> types) {
  if (!clipboard_listener_.is_bound() || !render_frame_host_->IsActive()) {
    return;
  }
  const absl::uint128 change_id = GetSequenceNumberImpl();
  if (last_change_id_ == change_id) {
    return;
  }
  last_change_id_ = change_id;
  clipboard_listener_->OnClipboardDataChanged(
      FilterClipboardChangeTypes(std::move(types)), change_id);
}

void CefNotifyClipboardChanged() {
  CefClipboardHost::NotifyClipboardChanged();
}
