/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/DeprecatedString.h>
#include <AK/Optional.h>
#include <AK/URL.h>
#include <AK/Vector.h>
#include <LibCore/Forward.h>

namespace HTTP {

class HttpRequest {
public:
    enum class ParseError {
        RequestTooLarge,
        RequestIncomplete,
        OutOfMemory,
        UnsupportedMethod
    };

    static StringView parse_error_to_string(ParseError error)
    {
        switch (error) {
        case ParseError::RequestTooLarge:
            return "Request too large"sv;
        case ParseError::RequestIncomplete:
            return "Request is incomplete"sv;
        case ParseError::OutOfMemory:
            return "Out of memory"sv;
        case ParseError::UnsupportedMethod:
            return "Unsupported method"sv;
        default:
            VERIFY_NOT_REACHED();
        }
    }

    enum Method {
        Invalid,
        HEAD,
        GET,
        POST,
        DELETE,
        PATCH,
        OPTIONS,
        TRACE,
        CONNECT,
        PUT,
    };

    struct Header {
        DeprecatedString name;
        DeprecatedString value;
    };

    struct BasicAuthenticationCredentials {
        DeprecatedString username;
        DeprecatedString password;
    };

    HttpRequest() = default;
    ~HttpRequest() = default;

    DeprecatedString const& resource() const { return m_resource; }
    Vector<Header> const& headers() const { return m_headers; }

    URL const& url() const { return m_url; }
    void set_url(URL const& url) { m_url = url; }

    Method method() const { return m_method; }
    void set_method(Method method) { m_method = method; }

    ByteBuffer const& body() const { return m_body; }
    void set_body(ByteBuffer&& body) { m_body = move(body); }

    StringView method_name() const;
    ErrorOr<ByteBuffer> to_raw_request() const;

    void set_headers(HashMap<DeprecatedString, DeprecatedString> const&);

    static ErrorOr<HttpRequest, HttpRequest::ParseError> from_raw_request(ReadonlyBytes);
    static Optional<Header> get_http_basic_authentication_header(URL const&);
    static Optional<BasicAuthenticationCredentials> parse_http_basic_authentication_header(DeprecatedString const&);

private:
    URL m_url;
    DeprecatedString m_resource;
    Method m_method { GET };
    Vector<Header> m_headers;
    ByteBuffer m_body;
};

StringView to_string_view(HttpRequest::Method);

}
