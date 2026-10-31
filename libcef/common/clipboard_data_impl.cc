// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "cef/libcef/common/clipboard_data_impl.h"

CefClipboardDataImpl::CefClipboardDataImpl() = default;

CefClipboardDataImpl::CefClipboardDataImpl(const CefClipboardDataImpl& other) {
  base::AutoLock lock_scope(other.lock_);
  formats_ = other.formats_;
  text_ = other.text_;
  html_ = other.html_;
  html_source_url_ = other.html_source_url_;
  rtf_ = other.rtf_;
  svg_ = other.svg_;
  image_ = other.image_ ? other.image_->Copy() : nullptr;
  file_paths_ = other.file_paths_;
  display_names_ = other.display_names_;
  bookmark_url_ = other.bookmark_url_;
  bookmark_title_ = other.bookmark_title_;
  custom_data_ = other.custom_data_;
  read_only_ = other.read_only_;
}

CefRefPtr<CefClipboardData> CefClipboardData::Create() {
  return new CefClipboardDataImpl();
}

CefRefPtr<CefClipboardData> CefClipboardDataImpl::Clone() {
  CefRefPtr<CefClipboardDataImpl> clone(new CefClipboardDataImpl(*this));
  clone->SetReadOnly(false);
  return clone;
}

bool CefClipboardDataImpl::IsReadOnly() {
  base::AutoLock lock_scope(lock_);
  return read_only_;
}

uint32_t CefClipboardDataImpl::GetAvailableFormats() {
  base::AutoLock lock_scope(lock_);
  return formats_;
}

void CefClipboardDataImpl::SetAvailableFormats(uint32_t formats) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    formats_ = formats;
  }
}

CefString CefClipboardDataImpl::GetText() {
  base::AutoLock lock_scope(lock_);
  return text_;
}

void CefClipboardDataImpl::SetText(const CefString& text) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    text_ = text;
    formats_ |= CEF_CLIPBOARD_FORMAT_TEXT;
  }
}

CefString CefClipboardDataImpl::GetHtml() {
  base::AutoLock lock_scope(lock_);
  return html_;
}

CefString CefClipboardDataImpl::GetHtmlSourceUrl() {
  base::AutoLock lock_scope(lock_);
  return html_source_url_;
}

void CefClipboardDataImpl::SetHtml(const CefString& markup,
                                   const CefString& source_url) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    html_ = markup;
    html_source_url_ = source_url;
    formats_ |= CEF_CLIPBOARD_FORMAT_HTML;
  }
}

CefString CefClipboardDataImpl::GetRtf() {
  base::AutoLock lock_scope(lock_);
  return rtf_;
}

void CefClipboardDataImpl::SetRtf(const CefString& rtf) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    rtf_ = rtf;
    formats_ |= CEF_CLIPBOARD_FORMAT_RTF;
  }
}

CefString CefClipboardDataImpl::GetSvg() {
  base::AutoLock lock_scope(lock_);
  return svg_;
}

void CefClipboardDataImpl::SetSvg(const CefString& svg) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    svg_ = svg;
    formats_ |= CEF_CLIPBOARD_FORMAT_SVG;
  }
}

CefRefPtr<CefBinaryValue> CefClipboardDataImpl::GetImage() {
  base::AutoLock lock_scope(lock_);
  return image_;
}

void CefClipboardDataImpl::SetImage(CefRefPtr<CefBinaryValue> png_data) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    image_ = png_data && png_data->IsValid() ? png_data->Copy() : nullptr;
    if (image_) {
      formats_ |= CEF_CLIPBOARD_FORMAT_IMAGE;
    } else {
      formats_ &= ~CEF_CLIPBOARD_FORMAT_IMAGE;
    }
  }
}

void CefClipboardDataImpl::GetFiles(std::vector<CefString>& file_paths,
                                    std::vector<CefString>& display_names) {
  base::AutoLock lock_scope(lock_);
  file_paths = file_paths_;
  display_names = display_names_;
}

void CefClipboardDataImpl::SetFiles(
    const std::vector<CefString>& file_paths,
    const std::vector<CefString>& display_names) {
  if (file_paths.size() != display_names.size()) {
    return;
  }
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    file_paths_ = file_paths;
    display_names_ = display_names;
    if (file_paths_.empty()) {
      formats_ &= ~CEF_CLIPBOARD_FORMAT_FILES;
    } else {
      formats_ |= CEF_CLIPBOARD_FORMAT_FILES;
    }
  }
}

CefString CefClipboardDataImpl::GetBookmarkUrl() {
  base::AutoLock lock_scope(lock_);
  return bookmark_url_;
}

CefString CefClipboardDataImpl::GetBookmarkTitle() {
  base::AutoLock lock_scope(lock_);
  return bookmark_title_;
}

void CefClipboardDataImpl::SetBookmark(const CefString& url,
                                       const CefString& title) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    bookmark_url_ = url;
    bookmark_title_ = title;
    formats_ |= CEF_CLIPBOARD_FORMAT_BOOKMARK;
  }
}

void CefClipboardDataImpl::GetCustomData(std::map<CefString, CefString>& data) {
  base::AutoLock lock_scope(lock_);
  data = custom_data_;
}

void CefClipboardDataImpl::SetCustomData(
    const std::map<CefString, CefString>& data) {
  base::AutoLock lock_scope(lock_);
  if (!read_only_) {
    custom_data_ = data;
    if (custom_data_.empty()) {
      formats_ &= ~CEF_CLIPBOARD_FORMAT_CUSTOM_DATA;
    } else {
      formats_ |= CEF_CLIPBOARD_FORMAT_CUSTOM_DATA;
    }
  }
}

void CefClipboardDataImpl::SetReadOnly(bool read_only) {
  base::AutoLock lock_scope(lock_);
  read_only_ = read_only;
}
