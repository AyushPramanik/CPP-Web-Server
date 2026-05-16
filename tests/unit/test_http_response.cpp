#include "http/response.hpp"

#include <gtest/gtest.h>

using cppws::http::HttpResponse;
using cppws::http::Status;

TEST(HttpResponseTest, SerializesStatusLine) {
  HttpResponse resp;
  resp.set_status(Status::Ok);
  const auto out = resp.serialize();
  EXPECT_EQ(out.substr(0, 15), "HTTP/1.1 200 OK");
}

TEST(HttpResponseTest, AutoAddsContentLength) {
  HttpResponse resp;
  resp.set_body("hello");
  const auto out = resp.serialize();
  EXPECT_NE(out.find("content-length: 5"), std::string::npos);
}

TEST(HttpResponseTest, BodyAppearsAfterBlankLine) {
  HttpResponse resp;
  resp.set_body("test body");
  const auto out = resp.serialize();
  const auto sep = out.find("\r\n\r\n");
  ASSERT_NE(sep, std::string::npos);
  EXPECT_EQ(out.substr(sep + 4), "test body");
}

TEST(HttpResponseTest, MakeErrorSetsStatusAndBody) {
  auto resp = HttpResponse::make_error(Status::NotFound, "page missing");
  EXPECT_EQ(resp.status, Status::NotFound);
  EXPECT_NE(resp.body.find("404"), std::string::npos);
  EXPECT_NE(resp.body.find("page missing"), std::string::npos);
}

TEST(HttpResponseTest, MakeRedirectSetsLocation) {
  auto resp = HttpResponse::make_redirect("/new/path");
  EXPECT_EQ(resp.status, Status::MovedPermanently);
  EXPECT_EQ(resp.headers.at("location"), "/new/path");
}

TEST(HttpResponseTest, SetHeaderLowercasesName) {
  HttpResponse resp;
  resp.set_header("X-Custom", "value");
  EXPECT_EQ(resp.headers.count("x-custom"), 1u);
}
