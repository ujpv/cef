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

#ifndef CEF_INCLUDE_CEF_CLIPBOARD_HANDLER_H_
#define CEF_INCLUDE_CEF_CLIPBOARD_HANDLER_H_
#pragma once

#include "include/cef_api_hash.h"
#include "include/cef_base.h"
#include "include/cef_browser.h"
#include "include/cef_clipboard_data.h"
#include "include/cef_frame.h"

#if CEF_API_ADDED(CEF_NEXT)
///
/// Callback used to complete an asynchronous clipboard read. The methods of
/// this class may be called on any thread.
///
/*--cef(source=library,added=next)--*/
class CefClipboardReadCallback : public virtual CefBaseRefCounted {
 public:
  ///
  /// Complete the read. Passing nullptr requests native clipboard fallback.
  /// If the callback is released without being continued, native fallback is
  /// used automatically.
  ///
  /*--cef(capi_name=cont,optional_param=data)--*/
  virtual void Continue(CefRefPtr<CefClipboardData> data) = 0;
};

///
/// Implement this interface to handle clipboard reads and writes for a browser
/// client. Calls are associated with the frame whose ClipboardHost interface
/// is being used. Read callbacks may be continued asynchronously and must not
/// be retained indefinitely.
///
/*--cef(source=client,added=next)--*/
class CefClipboardHandler : public virtual CefBaseRefCounted {
 public:
  ///
  /// Called when clipboard data is requested. |formats_hint| contains the
  /// formats requested by the renderer. Return true to handle the request and
  /// complete |callback|, or false to use the native clipboard. The callback
  /// may be continued on any thread.
  ///
  /*--cef()--*/
  virtual bool OnClipboardRead(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               cef_clipboard_buffer_t buffer,
                               uint32_t formats_hint,
                               CefRefPtr<CefClipboardReadCallback> callback) {
    return false;
  }

  ///
  /// Called when renderer clipboard data is committed. Return true to handle
  /// the write, or false to write the data to the native clipboard.
  ///
  /*--cef()--*/
  virtual bool OnClipboardWrite(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                cef_clipboard_buffer_t buffer,
                                CefRefPtr<CefClipboardData> data) {
    return false;
  }
};

///
/// Notify CEF that the embedder-owned clipboard contents have changed. This
/// updates clipboard sequence numbers and dispatches clipboard-change events.
/// May be called on any thread.
///
/*--cef(added=next)--*/
void CefNotifyClipboardChanged();
#endif  // CEF_API_ADDED(CEF_NEXT)

#endif  // CEF_INCLUDE_CEF_CLIPBOARD_HANDLER_H_
