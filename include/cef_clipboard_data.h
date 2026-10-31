// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the name Chromium Embedded
// Framework nor the names of its contributors may be used to endorse
// or promote products derived from this software without specific prior
// written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// ---------------------------------------------------------------------------
//
// The contents of this file must follow a specific format in order to
// support the CEF translator tool. See the translator.README.txt file in the
// tools directory for more information.
//

#ifndef CEF_INCLUDE_CEF_CLIPBOARD_DATA_H_
#define CEF_INCLUDE_CEF_CLIPBOARD_DATA_H_
#pragma once

#include <map>
#include <vector>

#include "include/cef_api_hash.h"
#include "include/cef_base.h"
#include "include/cef_values.h"
#include "include/internal/cef_types.h"

#if CEF_API_ADDED(CEF_NEXT)
///
/// Class used to represent clipboard data. Methods may be called on any
/// thread. Image data is represented as PNG-encoded bytes.
///
/*--cef(source=library,added=next)--*/
class CefClipboardData : public virtual CefBaseRefCounted {
 public:
  ///
  /// Create a new clipboard data object.
  ///
  /*--cef()--*/
  static CefRefPtr<CefClipboardData> Create();

  ///
  /// Returns a writable copy of this object.
  ///
  /*--cef()--*/
  virtual CefRefPtr<CefClipboardData> Clone() = 0;

  ///
  /// Returns true if this object is read-only. Objects passed to
  /// CefClipboardHandler::OnClipboardWrite are read-only; setter methods have
  /// no effect on read-only objects.
  ///
  /*--cef()--*/
  virtual bool IsReadOnly() = 0;

  ///
  /// Returns the available format bitmask. Handlers may set a format bit even
  /// when its payload will be supplied lazily by another means.
  ///
  /*--cef()--*/
  virtual uint32_t GetAvailableFormats() = 0;

  ///
  /// Sets the available format bitmask.
  ///
  /*--cef()--*/
  virtual void SetAvailableFormats(uint32_t formats) = 0;

  ///
  /// Returns the plain text data.
  ///
  /*--cef()--*/
  virtual CefString GetText() = 0;

  ///
  /// Sets the plain text data.
  ///
  /*--cef()--*/
  virtual void SetText(const CefString& text) = 0;

  ///
  /// Returns the HTML markup.
  ///
  /*--cef()--*/
  virtual CefString GetHtml() = 0;

  ///
  /// Returns the source URL associated with the HTML markup.
  ///
  /*--cef()--*/
  virtual CefString GetHtmlSourceUrl() = 0;

  ///
  /// Sets the HTML markup and its source URL.
  ///
  /*--cef()--*/
  virtual void SetHtml(const CefString& markup,
                       const CefString& source_url) = 0;

  ///
  /// Returns the RTF data.
  ///
  /*--cef()--*/
  virtual CefString GetRtf() = 0;

  ///
  /// Sets the RTF data.
  ///
  /*--cef()--*/
  virtual void SetRtf(const CefString& rtf) = 0;

  ///
  /// Returns the SVG markup.
  ///
  /*--cef()--*/
  virtual CefString GetSvg() = 0;

  ///
  /// Sets the SVG markup.
  ///
  /*--cef()--*/
  virtual void SetSvg(const CefString& svg) = 0;

  ///
  /// Returns PNG-encoded image data.
  ///
  /*--cef()--*/
  virtual CefRefPtr<CefBinaryValue> GetImage() = 0;

  ///
  /// Sets PNG-encoded image data. Pass nullptr to remove the image.
  ///
  /*--cef(optional_param=png_data)--*/
  virtual void SetImage(CefRefPtr<CefBinaryValue> png_data) = 0;

  ///
  /// Returns filesystem paths and display names for clipboard files.
  ///
  /*--cef()--*/
  virtual void GetFiles(std::vector<CefString>& file_paths,
                        std::vector<CefString>& display_names) = 0;

  ///
  /// Sets filesystem paths and display names for clipboard files. The vectors
  /// must have the same length.
  ///
  /*--cef()--*/
  virtual void SetFiles(const std::vector<CefString>& file_paths,
                        const std::vector<CefString>& display_names) = 0;

  ///
  /// Returns the bookmark URL.
  ///
  /*--cef()--*/
  virtual CefString GetBookmarkUrl() = 0;

  ///
  /// Returns the bookmark title.
  ///
  /*--cef()--*/
  virtual CefString GetBookmarkTitle() = 0;

  ///
  /// Sets the bookmark URL and title.
  ///
  /*--cef()--*/
  virtual void SetBookmark(const CefString& url, const CefString& title) = 0;

  ///
  /// Returns Chromium DataTransfer custom data.
  ///
  /*--cef()--*/
  virtual void GetCustomData(std::map<CefString, CefString>& data) = 0;

  ///
  /// Sets Chromium DataTransfer custom data.
  ///
  /*--cef()--*/
  virtual void SetCustomData(const std::map<CefString, CefString>& data) = 0;
};
#endif  // CEF_API_ADDED(CEF_NEXT)

#endif  // CEF_INCLUDE_CEF_CLIPBOARD_DATA_H_
