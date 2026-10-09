#!/usr/bin/env python3
"""Live HTTP checks for the account and upload examples (one server at a time)."""

import http.client
import json
import os
import pathlib
import socket
import sys
import tempfile
from urllib.parse import quote

from test_http_regressions import PORT, check, request, server


def response(method, target, body=None, content_type=None):
    connection = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    connection.putrequest(method, target)
    if content_type:
        connection.putheader("Content-Type", content_type)
    if body is not None:
        connection.putheader("Content-Length", str(len(body)))
    connection.endheaders(body)
    result = connection.getresponse()
    status, headers, payload = result.status, {key.lower(): value for key, value in result.getheaders()}, result.read()
    connection.close()
    return status, headers, payload


def post_json(target, value):
    return request("POST", target, json.dumps(value).encode(), "application/json")


def test_auth(binary):
    with tempfile.TemporaryDirectory(prefix="qb-account-http-") as directory:
        with server(binary, pathlib.Path(directory), "/"):
            status, data = post_json("/auth/register", {"username": "new-user", "password": "chosen-A", "email": "new@example.test"})
            check(status == 201 and data["user"]["username"] == "new-user", "registration stores a new account")
            status, data = post_json("/auth/login", {"username": "new-user", "password": "chosen-A"})
            check(status == 200 and data.get("token"), "registered account accepts its chosen password")
            for password in ("chosen-B", "wrong"):
                status, _ = post_json("/auth/login", {"username": "new-user", "password": password})
                check(status == 401, f"registered account rejects {password}")
            for username, password in (("admin", "admin123"), ("john", "password123"), ("manager", "manager123")):
                status, data = post_json("/auth/login", {"username": username, "password": password})
                check(status == 200 and data.get("token"), f"seeded {username} account still accepts its password")
                status, _ = post_json("/auth/login", {"username": username, "password": "chosen-B"})
                check(status == 401, f"seeded {username} account rejects another password")
            status, _ = post_json("/auth/login", {"username": "inactive", "password": "inactive123"})
            check(status == 403, "inactive account remains disabled")


def multipart_upload(filename, description, tags, tags_first=False):
    boundary = "qb-account-upload-witness"
    escaped_filename = filename.replace('"', '\\"')
    file_part = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{escaped_filename}"\r\n'
                 "Content-Type: text/plain\r\n\r\ncontents of special file\r\n")
    description_part = f'--{boundary}\r\nContent-Disposition: form-data; name="description"\r\n\r\n{description}\r\n'
    tags_part = f'--{boundary}\r\nContent-Disposition: form-data; name="tags"\r\n\r\n{tags}\r\n' if tags is not None else None
    parts = ([tags_part] if tags_first and tags_part is not None else []) + [file_part, description_part]
    if not tags_first and tags_part is not None:
        parts.append(tags_part)
    parts.append(f"--{boundary}--\r\n")
    return response("POST", "/api/upload", "".join(parts).encode(), f"multipart/form-data; boundary={boundary}")


def test_upload(binary):
    with tempfile.TemporaryDirectory(prefix="qb-upload-http-") as directory:
        root = pathlib.Path(directory)
        # Windows forbids quotes and angle brackets in on-disk filenames. The DOM
        # fixture exercises those characters there without claiming filesystem support.
        original = 'report # & " <>.txt' if os.name != "nt" else "report # &.txt"
        description = 'description <draft> & "quoted"'
        tags = 'alpha & beta, <tag>, "quote"'
        expected_tags = ["alpha & beta", "<tag>", '"quote"']
        with server(binary, root, "/browse"):
            status, headers, payload = multipart_upload(original, description, tags)
            data = json.loads(payload)
            check(status == 201 and data["original_filename"] == original, "special filename upload succeeds intact")
            stored = data["filename"]
            segment = quote(stored, safe="")
            check(headers.get("location") == "/api/files/" + segment, "Location carries an encoded filename segment")
            check(data["path"] == "/uploads/" + segment, "upload response path carries an encoded filename segment")
            status, metadata = request("GET", "/api/files/" + segment)
            check(status == 200 and metadata["description"] == description and metadata["tags"] == expected_tags,
                  "description and entered tags survive metadata lookup")
            status, listing = request("GET", "/api/files")
            entry = next(item for item in listing["files"] if item["filename"] == stored)
            check(status == 200 and entry["metadata"]["description"] == description
                  and entry["metadata"]["tags"] == expected_tags, "list preserves filename, description and entered tags")
            check(entry["path"] == "/uploads/" + segment, "list path encodes its filename segment")
            status, _, body = response("GET", "/uploads/" + segment)
            check(status == 200 and body == b"contents of special file", "encoded download returns original bytes")
            status, _ = request("DELETE", "/api/files/" + segment)
            check(status == 200 and not (root / "uploads" / stored).exists(), "encoded delete targets original filename")
            status, _, payload = multipart_upload("defaults.txt", "default tags", None)
            default_name = json.loads(payload)["filename"]
            status, metadata = request("GET", "/api/files/" + quote(default_name, safe=""))
            check(status == 200 and metadata["tags"] == ["uploaded", "api"], "omitted tags retain demo defaults")
            for rejected, tags_first in (("x" * 65, False), (",".join(["tag"] * 17), False),
                                         ("x," * (128 * 1024), True)):
                status, _, payload = multipart_upload("invalid.txt", "rejected tags", rejected, tags_first)
                check(status == 400 and json.loads(payload)["error"] == "Invalid tags", "invalid tags are rejected before storage")
            check(sorted(item.name for item in (root / "uploads").iterdir()) == [default_name],
                  "rejected uploads leave no files")
            status, _, payload = multipart_upload("after-rejected.txt", "still available", ",".join(["ok"] * 16))
            healthy_name = json.loads(payload)["filename"]
            status, metadata = request("GET", "/api/files/" + quote(healthy_name, safe=""))
            check(status == 200 and metadata["tags"] == ["ok"] * 16, "server accepts a follow-up upload with 16 tags")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("auth", "upload"):
        raise SystemExit("usage: test_http_account_upload.py <auth|upload> <example-binary>")
    with socket.socket() as probe:
        if probe.connect_ex(("127.0.0.1", PORT)) == 0:
            raise SystemExit("port 8080 is already in use")
    binary = pathlib.Path(sys.argv[2]).resolve()
    (test_auth if sys.argv[1] == "auth" else test_upload)(binary)


if __name__ == "__main__":
    main()
