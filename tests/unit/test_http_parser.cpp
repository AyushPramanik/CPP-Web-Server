#include "http/parser.hpp"

#include <gtest/gtest.h>

using cppws::http::HttpParser;
using cppws::http::Method;

class ParserTest : public ::testing::Test {
protected:
  HttpParser parser;
};

// ── Request line ──────────────────────────────────────────────────────────────

TEST_F(ParserTest, ParsesSimpleGetRequest) {
  const std::string req =
      "GET /index.html HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "\r\n";

  EXPECT_EQ(parser.feed(req), HttpParser::Result::Complete);
  const auto& result = parser.result();
  EXPECT_EQ(result.method, Method::GET);
  EXPECT_EQ(result.path, "/index.html");
  EXPECT_EQ(result.version, "HTTP/1.1");
}

TEST_F(ParserTest, ParsesPostWithBody) {
  const std::string req =
      "POST /submit HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 11\r\n"
      "Content-Type: application/x-www-form-urlencoded\r\n"
      "\r\n"
      "hello=world";

  EXPECT_EQ(parser.feed(req), HttpParser::Result::Complete);
  const auto& result = parser.result();
  EXPECT_EQ(result.method, Method::POST);
  EXPECT_EQ(result.path, "/submit");
  EXPECT_EQ(result.body, "hello=world");
}

TEST_F(ParserTest, HeadersAreLowercased) {
  const std::string req =
      "GET / HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "X-Custom-Header: VALUE\r\n"
      "\r\n";

  EXPECT_EQ(parser.feed(req), HttpParser::Result::Complete);
  const auto& result = parser.result();
  EXPECT_TRUE(result.header("host").has_value());
  EXPECT_EQ(*result.header("host"), "example.com");
  EXPECT_TRUE(result.header("x-custom-header").has_value());
  EXPECT_EQ(*result.header("x-custom-header"), "VALUE");
}

TEST_F(ParserTest, HeaderValuesAreTrimmed) {
  const std::string req =
      "GET / HTTP/1.1\r\n"
      "Host:   localhost   \r\n"
      "\r\n";

  EXPECT_EQ(parser.feed(req), HttpParser::Result::Complete);
  EXPECT_EQ(*parser.result().header("host"), "localhost");
}

// ── Partial reads ─────────────────────────────────────────────────────────────

TEST_F(ParserTest, HandlesPartialRequestLine) {
  EXPECT_EQ(parser.feed("GET /index"), HttpParser::Result::Incomplete);
  EXPECT_EQ(parser.feed(".html HTTP/1.1\r\n"), HttpParser::Result::Incomplete);
  EXPECT_EQ(parser.feed("Host: localhost\r\n\r\n"), HttpParser::Result::Complete);
  EXPECT_EQ(parser.result().path, "/index.html");
}

TEST_F(ParserTest, HandlesOneByteFeed) {
  const std::string req = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
  for (std::size_t i = 0; i + 1 < req.size(); ++i) {
    EXPECT_EQ(parser.feed(req.substr(i, 1)), HttpParser::Result::Incomplete);
  }
  EXPECT_EQ(parser.feed(req.substr(req.size() - 1, 1)), HttpParser::Result::Complete);
}

TEST_F(ParserTest, HandlesPartialBody) {
  EXPECT_EQ(parser.feed("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\n"),
            HttpParser::Result::Incomplete);
  EXPECT_EQ(parser.feed("hel"), HttpParser::Result::Incomplete);
  EXPECT_EQ(parser.feed("lo"), HttpParser::Result::Complete);
  EXPECT_EQ(parser.result().body, "hello");
}

// ── Error cases ───────────────────────────────────────────────────────────────

TEST_F(ParserTest, RejectsMalformedRequestLine) {
  EXPECT_EQ(parser.feed("BADREQUEST\r\n\r\n"), HttpParser::Result::Error);
  EXPECT_FALSE(parser.error().empty());
}

TEST_F(ParserTest, RejectsUnknownMethod) {
  EXPECT_EQ(parser.feed("BREW /coffee HTTP/1.1\r\n\r\n"), HttpParser::Result::Error);
}

TEST_F(ParserTest, RejectsUnsupportedVersion) {
  EXPECT_EQ(parser.feed("GET / HTTP/2.0\r\n\r\n"), HttpParser::Result::Error);
  EXPECT_NE(parser.error().find("unsupported"), std::string::npos);
}

TEST_F(ParserTest, RejectsMalformedHeader) {
  const std::string req = "GET / HTTP/1.1\r\nBadHeader\r\n\r\n";
  EXPECT_EQ(parser.feed(req), HttpParser::Result::Error);
}

// ── Reset / keep-alive ────────────────────────────────────────────────────────

TEST_F(ParserTest, ResetAllowsReuse) {
  const std::string req = "GET / HTTP/1.1\r\nHost: a\r\n\r\n";
  EXPECT_EQ(parser.feed(req), HttpParser::Result::Complete);
  parser.reset();
  EXPECT_EQ(parser.feed(req), HttpParser::Result::Complete);
  EXPECT_EQ(parser.result().method, Method::GET);
}

// ── Keep-alive detection ──────────────────────────────────────────────────────

TEST_F(ParserTest, Http11DefaultsToKeepAlive) {
  const std::string req = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
  parser.feed(req);
  EXPECT_TRUE(parser.result().wants_keep_alive());
}

TEST_F(ParserTest, Http11ConnectionCloseDisablesKeepAlive) {
  const std::string req = "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
  parser.feed(req);
  EXPECT_FALSE(parser.result().wants_keep_alive());
}

TEST_F(ParserTest, Http10DefaultsToClose) {
  const std::string req = "GET / HTTP/1.0\r\nHost: x\r\n\r\n";
  parser.feed(req);
  EXPECT_FALSE(parser.result().wants_keep_alive());
}
