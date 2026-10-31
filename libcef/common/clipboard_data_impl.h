// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_LIBCEF_COMMON_CLIPBOARD_DATA_IMPL_H_
#define CEF_LIBCEF_COMMON_CLIPBOARD_DATA_IMPL_H_
#pragma once

#include <map>
#include <vector>

#include "base/synchronization/lock.h"
#include "cef/include/cef_clipboard_data.h"

class CefClipboardDataImpl : public CefClipboardData {
 public:
  CefClipboardDataImpl();
  CefClipboardDataImpl(const CefClipboardDataImpl& other);

  CefRefPtr<CefClipboardData> Clone() override;
  bool IsReadOnly() override;
  uint32_t GetAvailableFormats() override;
  void SetAvailableFormats(uint32_t formats) override;
  CefString GetText() override;
  void SetText(const CefString& text) override;
  CefString GetHtml() override;
  CefString GetHtmlSourceUrl() override;
  void SetHtml(const CefString& markup, const CefString& source_url) override;
  CefString GetRtf() override;
  void SetRtf(const CefString& rtf) override;
  CefString GetSvg() override;
  void SetSvg(const CefString& svg) override;
  CefRefPtr<CefBinaryValue> GetImage() override;
  void SetImage(CefRefPtr<CefBinaryValue> png_data) override;
  void GetFiles(std::vector<CefString>& file_paths,
                std::vector<CefString>& display_names) override;
  void SetFiles(const std::vector<CefString>& file_paths,
                const std::vector<CefString>& display_names) override;
  CefString GetBookmarkUrl() override;
  CefString GetBookmarkTitle() override;
  void SetBookmark(const CefString& url, const CefString& title) override;
  void GetCustomData(std::map<CefString, CefString>& data) override;
  void SetCustomData(const std::map<CefString, CefString>& data) override;

  void SetReadOnly(bool read_only);

 private:
  uint32_t formats_ = CEF_CLIPBOARD_FORMAT_NONE;
  CefString text_;
  CefString html_;
  CefString html_source_url_;
  CefString rtf_;
  CefString svg_;
  CefRefPtr<CefBinaryValue> image_;
  std::vector<CefString> file_paths_;
  std::vector<CefString> display_names_;
  CefString bookmark_url_;
  CefString bookmark_title_;
  std::map<CefString, CefString> custom_data_;
  bool read_only_ = false;

  mutable base::Lock lock_;

  IMPLEMENT_REFCOUNTING(CefClipboardDataImpl);
};

#endif  // CEF_LIBCEF_COMMON_CLIPBOARD_DATA_IMPL_H_
