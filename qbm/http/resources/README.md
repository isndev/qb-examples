# QB HTTP Examples - Static Resources

This directory holds static assets and related frontend code for the `08_static_files.cpp` example, demonstrating file serving and upload capabilities of the `qb::http` module.

## Directory Structure

```
resources/
├── static/                 # Root for StaticFilesMiddleware in 08_static_files.cpp
│   ├── index.html          # Main entry point for the static demo
│   ├── about.html          # An example static HTML page
│   ├── upload.html         # Frontend for testing file uploads
│   ├── styles.css          # CSS styles for the demo pages
│   ├── main.js             # Basic JavaScript for frontend interactions
│   ├── upload.js           # JavaScript specific to the upload page functionality
│   ├── sample.txt          # A sample text file for download
│   ├── data.json           # Sample JSON data, served as a static file
│   └── (other assets...)
├── README.md               # This file
└── http2/                  # Resources for the HTTP/2 demo (12_http2_server.cpp)
    └── ... 
```

## Purpose of `static/` Directory

The `static/` subdirectory is primarily used by the `08_static_files.cpp` example. It showcases:

*   **Static File Serving**: How `qb::http::StaticFilesMiddleware` serves HTML, CSS, JavaScript, images, and other static content.
*   **MIME Type Handling**: Correct `Content-Type` headers are set based on file extensions.
*   **Caching Headers**: Demonstrates how the middleware can handle `ETag`, `Last-Modified`, and `Cache-Control` (though the example focuses on basic serving).
*   **Directory Traversal Protection**: The middleware prevents access to files outside the configured root directory.
*   **Frontend for API Interaction**: `upload.html` and `upload.js` provide a user interface to test the file upload API (`/api/upload`) and file management APIs (`/api/files`) implemented in `08_static_files.cpp`.

### Key Files in `static/`

*   **`index.html`**: The main landing page for the static file server example. It links to other demo pages and API endpoints.
*   **`about.html`**: A simple static HTML page to demonstrate basic file serving.
*   **`upload.html`**: An interactive page for uploading files. It uses JavaScript (`upload.js`) to communicate with the backend API.
*   **`styles.css`**: Provides styling for the HTML pages in this demo.
*   **`main.js`**: Contains general JavaScript utilities or interactions for the static demo pages.
*   **`upload.js`**: Contains the JavaScript logic for handling file uploads, listing files, and deleting files via the API endpoints provided by `08_static_files.cpp`.
*   **`sample.txt`**: A plain text file used to demonstrate serving different content types.
*   **`data.json`**: A JSON file served statically, illustrating that any file type can be served.

## Interaction with `08_static_files.cpp`

The `08_static_files.cpp` server is configured to:
1.  Serve files from the `./resources/static` directory under the `/static/` URL path.
    *   e.g., `http://localhost:8080/static/index.html` maps to `examples/qbm/http/resources/static/index.html`.
2.  Serve files from an `./uploads` directory (created by the example) under the `/uploads/` URL path. This directory is where uploaded files are stored.
3.  Provide a `/browse` endpoint for listing files in the `./uploads` directory.
4.  Offer API endpoints like `/api/upload` and `/api/files` that are typically interacted with from `upload.html`.

## Running the Static File Demo (`08_static_files.cpp`)

1.  Build and run the `08_static_files` executable.
2.  Open your browser and navigate to `http://localhost:8080/static/` or `http://localhost:8080/static/index.html`.
3.  Explore the links, especially the "File Upload Test" (`upload.html`), to interact with the backend.

## HTTP/2 Demo Static Resources (`http2/`)

This section details the static files located in the `resources/http2/` directory, which are specifically designed to demonstrate HTTP/2 features in the QB framework `12_http2_server.cpp` example.

### File Structure (`http2/`)

```
resources/http2/
├── index.html          # Main interactive demo page for HTTP/2 features
├── http2-demo.js       # JavaScript logic for the interactive HTTP/2 demo
├── styles.css          # CSS styles for the HTTP/2 demo page
├── data.json           # Sample JSON data used by some demo endpoints
├── favicon.ico         # Favicon for the demo page
└── README.md           # This documentation (now merged here)
```

### Purpose of `http2/` Directory

These files create an interactive web application served by `12_http2_server.cpp` to showcase and allow testing of various HTTP/2 capabilities. The server is configured to serve this directory as its static content root, typically at `https://localhost:8443/`.

#### Key Features Demonstrated by `index.html` and `http2-demo.js`:

*   **Request Multiplexing**: The demo allows initiating multiple requests simultaneously to visualize how HTTP/2 handles them over a single connection.
    *   Interacts with `/api/multiplexing-demo`.
*   **Server Push (Conceptual)**: The demo page explains server push and interacts with an endpoint (`/api/server-push-demo`) that simulates a scenario where resources would be pushed. The backend sends information about what *would* be pushed, and the frontend visualizes this.
*   **Stream Prioritization**: Buttons allow sending requests with different conceptual priority levels to an endpoint (`/api/stream-priority/:level`) that acknowledges the intended priority.
*   **Performance Testing**: A simple performance test can be run to make multiple requests to an endpoint (`/api/performance/:iterations`) and display basic metrics like response times.
*   **General HTTP/2 Interaction**: All assets (HTML, JS, CSS, JSON) are served over HTTP/2, allowing observation of HTTP/2 protocol behavior using browser developer tools.

#### File Descriptions in `http2/`:

*   **`index.html`**: The main HTML page that structures the interactive demo. It includes sections for Overview, Multiplexing, Server Push, Stream Priority, and Performance.
*   **`http2-demo.js`**: Contains all the client-side JavaScript logic to power the interactive features of `index.html`. This includes:
    *   Fetching data from the server's API endpoints.
    *   Updating the UI with results and visualizations.
    *   Handling user interactions (button clicks).
    *   A simple performance chart.
*   **`styles.css`**: Provides the visual styling for `index.html` to make the demo user-friendly.
*   **`data.json`**: A sample JSON file that might be used by some of the demo endpoints (e.g., fetched by the client or pushed by the server in a more advanced scenario). In this example, it's primarily available to be served like any other static asset.
*   **`favicon.ico`**: A placeholder favicon for the demo.

### Integration with `12_http2_server.cpp`

The `12_http2_server.cpp` example is configured to:

1.  Act as an HTTP/2 server (typically on `https://localhost:8443`).
2.  Use `qb::http::StaticFilesMiddleware` to serve files from the `examples/qbm/http/resources/http2/` directory as its root. For example, accessing `/index.html` on the server will serve `resources/http2/index.html`.
3.  Provide specific API endpoints (e.g., `/api/multiplexing-demo`, `/api/server-push-demo`, etc.) that the `http2-demo.js` script interacts with to demonstrate features.
4.  Requires SSL/TLS with ALPN for HTTP/2, and includes logic to generate self-signed certificates for ease of testing.

### Testing the HTTP/2 Demo (`12_http2_server.cpp`)

1.  Build and run the `12_http2_server` executable.
2.  Open your browser and navigate to `https://localhost:8443/` (or `https://localhost:8443/index.html`). You may need to accept a security warning due to the self-signed certificate.
3.  Interact with the different sections of the demo page to test HTTP/2 features.
4.  Use your browser's developer tools (Network tab) to observe:
    *   The protocol version (should be h2 or HTTP/2).
    *   A single connection being used for multiple requests (multiplexing).
    *   Headers (look for `:scheme`, `:method`, `:path`, `:authority` pseudo-headers).
    *   Timings of requests.


## General Features Demonstrated by Static Resources

Overall, the static resources in this `resources` directory (for both `static/` and `http2/` subdirectories) help demonstrate:

1. **Static File Serving**
   - Proper MIME type detection
   - Caching headers (ETag, Last-Modified) - more emphasized in `08_static_files`
   - Range request support
   - Directory browsing (`08_static_files`)

2. **File Upload & Management (via `static/upload.html` and `08_static_files.cpp` API)**
   - Multipart form-data parsing
   - File size validation (e.g., 50MB limit in `08_static_files.cpp`)
   - Metadata storage and retrieval
   - Secure filename generation

3. **RESTful API Interaction (from JS)**
   - JSON responses
   - Error handling
   - CRUD operations for files (via `static/upload.js` for `08_static_files.cpp`)
   - Proper HTTP status codes

4. **Security Features (mainly by the server examples)**
   - Path traversal protection
   - File size limits
   - Input validation
   - Safe filename handling

5. **Modern Web Standards in Frontend Files**
   - Responsive design (basic)
   - Progressive enhancement concepts
   - Cross-browser compatibility considerations

## Development Notes

- Files are copied from `examples/qbm/http/resources/` to the build directory during CMake configuration.
- The example servers (`08_static_files.cpp`, `12_http2_server.cpp`) serve these files directly from the filesystem relative to their configured static root paths.
- Modifications to these files typically require rebuilding the relevant example to see changes if the server is already running, or simply refreshing the browser if the server serves them dynamically (which is the case for these examples).
- The `upload.html` page and `http2-demo.js` use modern JavaScript features (async/await, Fetch API).

## Testing the Demos

- For the static file server and upload demo (`08_static_files.cpp`):
    1. Build and run the `08_static_files` example.
    2. Open `http://localhost:8080/static/` in your browser.
    3. Navigate through the various pages and test file upload.
    4. Use curl commands to test the API endpoints.

- For the HTTP/2 demo (`12_http2_server.cpp`):
    1. Build and run the `12_http2_server` example.
    2. Open `https://localhost:8443/` in your browser (accept self-signed certificate warning).
    3. Interact with the demo sections.

## Browser Support (for HTML/JS/CSS files)

The web interfaces generally support:
- Chrome 60+
- Firefox 55+
- Safari 11+
- Edge 79+

Older browsers may experience reduced functionality due to modern JavaScript usage. 