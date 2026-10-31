// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_clipboard_data.h"
#include "include/cef_clipboard_handler.h"
#include "include/cef_task.h"
#include "include/cef_waitable_event.h"
#include "include/wrapper/cef_closure_task.h"
#include "tests/ceftests/test_handler.h"
#include "tests/ceftests/test_util.h"
#include "tests/gtest/include/gtest/gtest.h"

namespace {

const char kClipboardUrl[] = "https://tests-clipboard/page.html";

// Text selected and copied from the page.
const char kSourceText[] = "copied from page";

// Data supplied by the handler when it services a read.
const char kHandlerText[] = "pasted from handler";
const char kHandlerHtml[] = "<b>pasted from handler</b>";

// Title prefix reported by the page after a paste completes. The title has the
// format "pasted:<pasted text>|<paste event types>|<paste event html>".
const char kPastedTitlePrefix[] = "pasted:";

enum class HandlerMode {
  // CefClient::GetClipboardHandler() returns nullptr; the stock Chromium
  // ClipboardHost is used.
  kNone,
  // The handler declines all reads and writes; the native clipboard is used.
  kDecline,
  // The handler services all reads and writes; the native clipboard is never
  // touched.
  kHandle,
};

std::string GetPageHtml() {
  return "<html><head><script>"
         "function selectSource() {"
         "  var src = document.getElementById('src');"
         "  src.focus();"
         "  src.select();"
         "  document.title = 'selected';"
         "}"
         "function focusDest() {"
         "  document.getElementById('dst').focus();"
         "  document.title = 'ready';"
         "}"
         "window.onload = function() {"
         "  var dst = document.getElementById('dst');"
         "  document.addEventListener('copy', function() {"
         // Runs after the copy command has queued its clipboard writes.
         "    setTimeout(focusDest, 0);"
         "  });"
         "  dst.addEventListener('paste', function(e) {"
         "    window.pasteTypes = Array.from(e.clipboardData.types).join(',');"
         "    window.pasteHtml = e.clipboardData.getData('text/html');"
         "  });"
         "  dst.addEventListener('input', function() {"
         "    document.title = 'pasted:' + dst.textContent + '|' +"
         "        window.pasteTypes + '|' + window.pasteHtml;"
         "  });"
         "};"
         "</script></head><body>"
         // Copying from a text control writes text/plain only.
         "<textarea id=\"src\">" +
         std::string(kSourceText) +
         "</textarea>"
         // The paste target must be richly editable, otherwise the paste event
         // only exposes text/plain.
         "<div id=\"dst\" contenteditable=\"true\"></div>"
         "</body></html>";
}

class ClipboardTestHandler : public TestHandler, public CefClipboardHandler {
 public:
  explicit ClipboardTestHandler(HandlerMode mode) : mode_(mode) {}

  // CefClient methods:
  CefRefPtr<CefClipboardHandler> GetClipboardHandler() override {
    got_get_clipboard_handler_.yes();
    if (mode_ == HandlerMode::kNone) {
      return nullptr;
    }
    return this;
  }

  // CefClipboardHandler methods:
  bool OnClipboardRead(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       cef_clipboard_buffer_t buffer,
                       uint32_t formats_hint,
                       CefRefPtr<CefClipboardReadCallback> callback) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_UI));
    EXPECT_NE(HandlerMode::kNone, mode_);
    EXPECT_TRUE(browser.get());
    EXPECT_EQ(GetBrowserId(), browser->GetIdentifier());
    EXPECT_TRUE(frame.get());
    EXPECT_TRUE(frame->IsMain());
    EXPECT_EQ(CEF_CLIPBOARD_BUFFER_STANDARD, buffer);
    EXPECT_TRUE(callback.get());
    read_count_++;

    if (mode_ == HandlerMode::kDecline) {
      return false;
    }

    CefRefPtr<CefClipboardData> data = CefClipboardData::Create();
    EXPECT_FALSE(data->IsReadOnly());
    EXPECT_EQ(static_cast<uint32_t>(CEF_CLIPBOARD_FORMAT_NONE),
              data->GetAvailableFormats());
    data->SetText(kHandlerText);
    data->SetHtml(kHandlerHtml, kClipboardUrl);
    EXPECT_EQ(static_cast<uint32_t>(CEF_CLIPBOARD_FORMAT_TEXT |
                                    CEF_CLIPBOARD_FORMAT_HTML),
              data->GetAvailableFormats());

    // Continue asynchronously from a different thread to verify that the
    // callback is thread-safe.
    CefPostTask(TID_IO, base::BindOnce(
                            [](CefRefPtr<CefClipboardReadCallback> callback,
                               CefRefPtr<CefClipboardData> data) {
                              EXPECT_TRUE(CefCurrentlyOn(TID_IO));
                              callback->Continue(data);
                            },
                            callback, data));
    return true;
  }

  bool OnClipboardWrite(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        cef_clipboard_buffer_t buffer,
                        CefRefPtr<CefClipboardData> data) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_UI));
    EXPECT_NE(HandlerMode::kNone, mode_);
    EXPECT_TRUE(browser.get());
    EXPECT_EQ(GetBrowserId(), browser->GetIdentifier());
    EXPECT_TRUE(frame.get());
    EXPECT_TRUE(frame->IsMain());
    EXPECT_EQ(CEF_CLIPBOARD_BUFFER_STANDARD, buffer);
    EXPECT_TRUE(data.get());
    EXPECT_TRUE(data->IsReadOnly());
    EXPECT_TRUE(data->GetAvailableFormats() & CEF_CLIPBOARD_FORMAT_TEXT);
    EXPECT_STREQ(kSourceText, data->GetText().ToString().c_str());

    // Verify that the object cannot be modified.
    data->SetText("modified");
    EXPECT_STREQ(kSourceText, data->GetText().ToString().c_str());

    // Verify that the clone is writable and independent.
    CefRefPtr<CefClipboardData> clone = data->Clone();
    EXPECT_TRUE(clone.get());
    EXPECT_FALSE(clone->IsReadOnly());
    EXPECT_STREQ(kSourceText, clone->GetText().ToString().c_str());
    clone->SetText("modified");
    EXPECT_STREQ("modified", clone->GetText().ToString().c_str());
    EXPECT_STREQ(kSourceText, data->GetText().ToString().c_str());

    write_count_++;
    return mode_ == HandlerMode::kHandle;
  }

  // TestHandler methods:
  void RunTest() override {
    AddResource(kClipboardUrl, GetPageHtml(), "text/html");
    CreateBrowser(kClipboardUrl);
    SetTestTimeout();
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override {
    if (!frame->IsMain()) {
      return;
    }
    got_load_end_.yes();
    browser->GetHost()->SetFocus(true);
    frame->ExecuteJavaScript("selectSource();", kClipboardUrl, 0);
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override {
    EXPECT_TRUE(CefCurrentlyOn(TID_UI));
    const std::string title_str = title;

    if (title_str == "selected") {
      if (got_selected_) {
        return;
      }
      got_selected_.yes();
      browser->GetMainFrame()->Copy();
    } else if (title_str == "ready") {
      if (got_ready_) {
        return;
      }
      got_ready_.yes();
      browser->GetMainFrame()->Paste();
    } else if (title_str.find(kPastedTitlePrefix) == 0) {
      if (got_pasted_) {
        return;
      }
      got_pasted_.yes();
      pasted_title_ = title_str.substr(strlen(kPastedTitlePrefix));
      DestroyTest();
    }
  }

  void DestroyTest() override {
    EXPECT_TRUE(got_load_end_);
    EXPECT_TRUE(got_selected_);
    EXPECT_TRUE(got_ready_);
    EXPECT_TRUE(got_pasted_);
    EXPECT_TRUE(got_get_clipboard_handler_);

    // Split "<value>|<types>|<html>".
    const size_t sep1 = pasted_title_.find('|');
    ASSERT_NE(std::string::npos, sep1);
    const size_t sep2 = pasted_title_.find('|', sep1 + 1);
    ASSERT_NE(std::string::npos, sep2);
    const std::string value = pasted_title_.substr(0, sep1);
    const std::string types = pasted_title_.substr(sep1 + 1, sep2 - sep1 - 1);
    const std::string html = pasted_title_.substr(sep2 + 1);

    if (mode_ == HandlerMode::kHandle) {
      // All reads were answered by the handler.
      EXPECT_STREQ(kHandlerText, value.c_str());
      EXPECT_NE(std::string::npos, types.find("text/plain")) << types;
      EXPECT_NE(std::string::npos, types.find("text/html")) << types;
      EXPECT_STREQ(kHandlerHtml, html.c_str());
      EXPECT_EQ(1, write_count_);
      EXPECT_GE(read_count_, 1);
    } else {
      // The native clipboard round-trip returned the copied text.
      EXPECT_STREQ(kSourceText, value.c_str());
      EXPECT_NE(std::string::npos, types.find("text/plain")) << types;
      if (mode_ == HandlerMode::kDecline) {
        EXPECT_EQ(1, write_count_);
        EXPECT_GE(read_count_, 1);
      } else {
        EXPECT_EQ(0, write_count_);
        EXPECT_EQ(0, read_count_);
      }
    }

    TestHandler::DestroyTest();
  }

 private:
  const HandlerMode mode_;

  int read_count_ = 0;
  int write_count_ = 0;
  std::string pasted_title_;

  TrackCallback got_get_clipboard_handler_;
  TrackCallback got_load_end_;
  TrackCallback got_selected_;
  TrackCallback got_ready_;
  TrackCallback got_pasted_;

  IMPLEMENT_REFCOUNTING(ClipboardTestHandler);
};

}  // namespace

// Copy/paste with no CefClipboardHandler uses the stock Chromium
// implementation.
TEST(ClipboardHandlerTest, NoHandler) {
  CefRefPtr<ClipboardTestHandler> handler =
      new ClipboardTestHandler(HandlerMode::kNone);
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}

// A handler that declines reads and writes falls back to the native clipboard.
TEST(ClipboardHandlerTest, DeclineFallsBackToNative) {
  CefRefPtr<ClipboardTestHandler> handler =
      new ClipboardTestHandler(HandlerMode::kDecline);
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}

// A handler that services reads and writes replaces the native clipboard for
// its client, including asynchronous read completion from another thread.
TEST(ClipboardHandlerTest, HandleReadWrite) {
  CefRefPtr<ClipboardTestHandler> handler =
      new ClipboardTestHandler(HandlerMode::kHandle);
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}

// Standalone CefClipboardData behavior.
TEST(ClipboardHandlerTest, DataObject) {
  CefRefPtr<CefClipboardData> data = CefClipboardData::Create();
  EXPECT_TRUE(data.get());
  EXPECT_FALSE(data->IsReadOnly());
  EXPECT_EQ(static_cast<uint32_t>(CEF_CLIPBOARD_FORMAT_NONE),
            data->GetAvailableFormats());

  data->SetText("text");
  data->SetHtml("<b>html</b>", "https://example.com/");
  data->SetRtf("{\\rtf1}");
  data->SetSvg("<svg/>");
  data->SetBookmark("https://example.com/", "Example");

  const unsigned char png[] = {0x89, 'P', 'N', 'G'};
  data->SetImage(CefBinaryValue::Create(png, sizeof(png)));

  std::vector<CefString> paths{"/tmp/a.txt", "/tmp/b.txt"};
  std::vector<CefString> names{"a.txt", "b.txt"};
  data->SetFiles(paths, names);

  std::map<CefString, CefString> custom{{"application/x-test", "value"}};
  data->SetCustomData(custom);

  const uint32_t expected =
      CEF_CLIPBOARD_FORMAT_TEXT | CEF_CLIPBOARD_FORMAT_HTML |
      CEF_CLIPBOARD_FORMAT_RTF | CEF_CLIPBOARD_FORMAT_SVG |
      CEF_CLIPBOARD_FORMAT_IMAGE | CEF_CLIPBOARD_FORMAT_FILES |
      CEF_CLIPBOARD_FORMAT_BOOKMARK | CEF_CLIPBOARD_FORMAT_CUSTOM_DATA;
  EXPECT_EQ(expected, data->GetAvailableFormats());

  EXPECT_STREQ("text", data->GetText().ToString().c_str());
  EXPECT_STREQ("<b>html</b>", data->GetHtml().ToString().c_str());
  EXPECT_STREQ("https://example.com/",
               data->GetHtmlSourceUrl().ToString().c_str());
  EXPECT_STREQ("{\\rtf1}", data->GetRtf().ToString().c_str());
  EXPECT_STREQ("<svg/>", data->GetSvg().ToString().c_str());
  EXPECT_STREQ("https://example.com/",
               data->GetBookmarkUrl().ToString().c_str());
  EXPECT_STREQ("Example", data->GetBookmarkTitle().ToString().c_str());

  CefRefPtr<CefBinaryValue> image = data->GetImage();
  ASSERT_TRUE(image.get());
  EXPECT_EQ(sizeof(png), image->GetSize());

  std::vector<CefString> out_paths, out_names;
  data->GetFiles(out_paths, out_names);
  ASSERT_EQ(2U, out_paths.size());
  ASSERT_EQ(2U, out_names.size());
  EXPECT_STREQ("/tmp/b.txt", out_paths[1].ToString().c_str());
  EXPECT_STREQ("b.txt", out_names[1].ToString().c_str());

  // Mismatched vector sizes are rejected.
  data->SetFiles(paths, std::vector<CefString>{"only-one"});
  data->GetFiles(out_paths, out_names);
  EXPECT_EQ(2U, out_paths.size());

  std::map<CefString, CefString> out_custom;
  data->GetCustomData(out_custom);
  ASSERT_EQ(1U, out_custom.size());
  EXPECT_STREQ("value",
               out_custom[CefString("application/x-test")].ToString().c_str());

  // Clearing optional payloads removes the corresponding format bits.
  data->SetImage(nullptr);
  data->SetFiles({}, {});
  data->SetCustomData({});
  EXPECT_EQ(
      expected & ~(CEF_CLIPBOARD_FORMAT_IMAGE | CEF_CLIPBOARD_FORMAT_FILES |
                   CEF_CLIPBOARD_FORMAT_CUSTOM_DATA),
      data->GetAvailableFormats());

  // Formats can be advertised without payloads.
  data->SetAvailableFormats(CEF_CLIPBOARD_FORMAT_IMAGE);
  EXPECT_EQ(static_cast<uint32_t>(CEF_CLIPBOARD_FORMAT_IMAGE),
            data->GetAvailableFormats());

  // Clones are deep copies.
  CefRefPtr<CefClipboardData> clone = data->Clone();
  ASSERT_TRUE(clone.get());
  EXPECT_STREQ("text", clone->GetText().ToString().c_str());
  clone->SetText("other");
  EXPECT_STREQ("text", data->GetText().ToString().c_str());
  EXPECT_STREQ("other", clone->GetText().ToString().c_str());
}

// CefNotifyClipboardChanged() may be called from any thread without a browser.
TEST(ClipboardHandlerTest, NotifyClipboardChanged) {
  CefNotifyClipboardChanged();

  CefRefPtr<CefWaitableEvent> event =
      CefWaitableEvent::CreateWaitableEvent(true, false);
  CefPostTask(TID_IO, base::BindOnce(
                          [](CefRefPtr<CefWaitableEvent> event) {
                            CefNotifyClipboardChanged();
                            event->Signal();
                          },
                          event));
  event->Wait();
}
