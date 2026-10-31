// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_CLIPBOARD_HOST_H_
#define CEF_LIBCEF_BROWSER_CLIPBOARD_HOST_H_
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "cef/include/cef_clipboard_handler.h"
#include "cef/libcef/common/clipboard_data_impl.h"
#include "content/browser/renderer_host/clipboard_host_impl.h"
#include "content/public/browser/document_service.h"
#include "third_party/abseil-cpp/absl/numeric/int128.h"
#include "third_party/blink/public/mojom/clipboard/clipboard.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/clipboard_observer.h"
#include "ui/base/clipboard/clipboard_sequence_number_token.h"
#include "url/gurl.h"

namespace content {
class RenderFrameHost;
}

class CefBrowserHostBase;

class CefClipboardHost final
    : public content::DocumentService<blink::mojom::ClipboardHost>,
      public ui::ClipboardObserver {
 public:
  CefClipboardHost(content::RenderFrameHost& render_frame_host,
                   mojo::PendingReceiver<blink::mojom::ClipboardHost> receiver,
                   CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   CefRefPtr<CefClipboardHandler> handler);
  ~CefClipboardHost() override;

  static void NotifyClipboardChanged();

  void GetSequenceNumber(ui::ClipboardBuffer buffer,
                         GetSequenceNumberCallback callback) override;
  void IsFormatAvailable(blink::mojom::ClipboardFormat format,
                         ui::ClipboardBuffer buffer,
                         IsFormatAvailableCallback callback) override;
  void ReadAvailableTypes(ui::ClipboardBuffer buffer,
                          ReadAvailableTypesCallback callback) override;
  void ReadText(ui::ClipboardBuffer buffer, ReadTextCallback callback) override;
  void ReadHtml(ui::ClipboardBuffer buffer, ReadHtmlCallback callback) override;
  void ReadSvg(ui::ClipboardBuffer buffer, ReadSvgCallback callback) override;
  void ReadRtf(ui::ClipboardBuffer buffer, ReadRtfCallback callback) override;
  void ReadPng(ui::ClipboardBuffer buffer, ReadPngCallback callback) override;
  void ReadFiles(ui::ClipboardBuffer buffer,
                 ReadFilesCallback callback) override;
  void ReadDataTransferCustomData(
      ui::ClipboardBuffer buffer,
      const std::u16string& type,
      ReadDataTransferCustomDataCallback callback) override;
  void ReadAvailableCustomAndStandardFormats(
      ReadAvailableCustomAndStandardFormatsCallback callback) override;
  void ReadUnsanitizedCustomFormat(
      const std::u16string& format,
      ReadUnsanitizedCustomFormatCallback callback) override;
  void WriteUnsanitizedCustomFormat(const std::u16string& format,
                                    mojo_base::BigBuffer data) override;
  void WriteText(const std::u16string& text) override;
  void WriteHtml(const std::u16string& markup, const GURL& url) override;
  void WriteSvg(const std::u16string& markup) override;
  void WriteSmartPasteMarker() override;
  void WriteDataTransferCustomData(
      const base::flat_map<std::u16string, std::u16string>& data) override;
  void WriteBookmark(const std::string& url,
                     const std::u16string& title) override;
  void WriteImage(const SkBitmap& unsafe_bitmap) override;
  void CommitWrite() override;
#if BUILDFLAG(IS_MAC)
  void WriteStringToFindPboard(const std::u16string& text) override;
  void GetPlatformPermissionState(
      GetPlatformPermissionStateCallback callback) override;
#endif
  void RegisterClipboardListener(
      mojo::PendingRemote<blink::mojom::ClipboardListener> listener) override;
  void OnClipboardDataChanged() override;

 private:
  using ReadDataCallback =
      base::OnceCallback<void(CefRefPtr<CefClipboardData>)>;

  bool IsPasteAllowed() const;
  uint32_t GetFormatsHint(blink::mojom::ClipboardFormat format) const;
  void RequestData(ui::ClipboardBuffer buffer,
                   uint32_t formats_hint,
                   ReadDataCallback callback);
  void WritePendingDataToNativeClipboard();
  void NotifyListenerWithData(CefRefPtr<CefClipboardData> data);
  void NotifyListenerWithTypes(std::vector<std::u16string> types);
  absl::uint128 GetSequenceNumberImpl() const;

  const raw_ref<content::RenderFrameHost> render_frame_host_;
  const CefRefPtr<CefBrowser> browser_;
  const CefRefPtr<CefFrame> frame_;
  const CefRefPtr<CefClipboardHandler> handler_;
  std::unique_ptr<content::ClipboardHostImpl> fallback_;
  std::unique_ptr<CefClipboardDataImpl> pending_data_;
  std::vector<std::pair<std::u16string, mojo_base::BigBuffer>>
      pending_unsanitized_formats_;
  bool pending_smart_paste_ = false;
  mojo::Remote<blink::mojom::ClipboardListener> clipboard_listener_;
  bool observing_clipboard_ = false;
  absl::uint128 last_change_id_ = 0;
  base::WeakPtrFactory<CefClipboardHost> weak_ptr_factory_{this};
};

#endif  // CEF_LIBCEF_BROWSER_CLIPBOARD_HOST_H_
