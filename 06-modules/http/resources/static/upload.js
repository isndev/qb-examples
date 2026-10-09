// Upload form handling
document.getElementById('uploadForm').addEventListener('submit', function (e) {
    e.preventDefault();

    const fileInput = document.getElementById('fileInput');
    const descriptionInput = document.getElementById('descriptionInput');
    const tagsInput = document.getElementById('tagsInput');
    const progressDiv = document.getElementById('uploadProgress');
    const statusDiv = document.getElementById('uploadStatus');

    if (!fileInput.files[0]) {
        showResult('Please select a file to upload.', 'danger');
        return;
    }

    const formData = new FormData();
    formData.append('file', fileInput.files[0]);
    if (descriptionInput.value.trim()) {
        formData.append('description', descriptionInput.value.trim());
    }
    if (tagsInput.value.trim()) {
        formData.append('tags', tagsInput.value.trim());
    }

    // Show progress
    progressDiv.classList.remove('hidden');
    statusDiv.classList.remove('hidden');
    statusDiv.className = 'alert alert-info';
    statusDiv.textContent = '';

    fetch('/api/upload', {
        method: 'POST',
        body: formData
    })
        .then(response => response.json())
        .then(result => {
            if (result.success) {
                showResult('✓ File uploaded successfully: ' + result.filename, 'success');
                document.getElementById('uploadForm').reset();
                loadFilesList();
            } else {
                showResult('✗ Upload failed: ' + (result.error || 'Unknown error'), 'danger');
            }
        })
        .catch(error => {
            showResult('✗ Upload error: ' + error.message, 'danger');
        })
        .finally(() => {
            progressDiv.classList.add('hidden');
        });
});

// Clear form button
document.getElementById('clearBtn').addEventListener('click', function () {
    document.getElementById('uploadForm').reset();
});

// Refresh files button
document.getElementById('refreshBtn').addEventListener('click', function () {
    loadFilesList();
});

// Show result message
function showResult(message, type) {
    const statusDiv = document.getElementById('uploadStatus');
    statusDiv.className = 'alert alert-' + type;
    statusDiv.textContent = message;
    statusDiv.classList.remove('hidden');
}

// Load files list
function loadFilesList() {
    const filesDiv = document.getElementById('filesList');

    fetch('/api/files')
        .then(response => response.json())
        .then(data => {
            if (!data.files || data.files.length === 0) {
                const empty = document.createElement('p');
                const message = document.createElement('em');
                message.textContent = 'No files uploaded yet.';
                empty.appendChild(message);
                filesDiv.replaceChildren(empty);
                return;
            }

            const table = document.createElement('table');
            const head = document.createElement('thead');
            const heading = document.createElement('tr');
            ['Filename', 'Size', 'Type', 'Uploaded', 'Actions'].forEach(label => {
                const cell = document.createElement('th');
                cell.textContent = label;
                heading.appendChild(cell);
            });
            head.appendChild(heading);
            table.appendChild(head);
            const body = document.createElement('tbody');

            data.files.forEach(file => {
                const fileSize = QBHttpUtils.formatFileSize(file.size);

                // Handle both formats: with and without metadata
                let mimeType = 'Unknown';
                let uploadDate = 'Unknown';
                let description = '';
                let tags = [];

                if (file.metadata) {
                    mimeType = file.metadata.mime_type || 'Unknown';
                    uploadDate = file.metadata.last_modified ?
                        new Date(file.metadata.last_modified * 1000).toLocaleString() : 'Unknown';
                    description = file.metadata.description || '';
                    tags = file.metadata.tags || [];
                }

                const row = document.createElement('tr');
                const nameCell = document.createElement('td');
                const name = document.createElement('strong');
                name.textContent = file.filename;
                nameCell.appendChild(name);
                if (description) {
                    const detail = document.createElement('small');
                    detail.textContent = description;
                    nameCell.appendChild(document.createElement('br'));
                    nameCell.appendChild(detail);
                }
                if (tags.length > 0) {
                    const detail = document.createElement('small');
                    detail.textContent = 'Tags: ' + tags.join(', ');
                    nameCell.appendChild(document.createElement('br'));
                    nameCell.appendChild(detail);
                }
                row.appendChild(nameCell);
                [fileSize, mimeType, uploadDate].forEach(value => {
                    const cell = document.createElement('td');
                    cell.textContent = value;
                    row.appendChild(cell);
                });

                const actions = document.createElement('td');
                const download = document.createElement('a');
                download.href = '/uploads/' + encodeURIComponent(file.filename);
                download.className = 'btn';
                download.target = '_blank';
                download.rel = 'noopener';
                download.textContent = 'Download';
                actions.appendChild(download);
                actions.appendChild(document.createTextNode(' '));

                const remove = document.createElement('button');
                remove.type = 'button';
                remove.className = 'btn btn-danger delete-btn';
                remove.dataset.filename = file.filename;
                remove.textContent = 'Delete';
                actions.appendChild(remove);
                row.appendChild(actions);
                body.appendChild(row);
            });

            table.appendChild(body);
            filesDiv.replaceChildren(table);

            // Attach event listeners to delete buttons (event delegation)
            attachDeleteHandlers();

        })
        .catch(error => {
            console.error('Load files error:', error);
            const message = document.createElement('p');
            message.className = 'alert alert-danger';
            message.textContent = 'Error loading files: ' + error.message;
            filesDiv.replaceChildren(message);
        });
}

// Attach delete button handlers using event delegation
function attachDeleteHandlers() {
    const filesDiv = document.getElementById('filesList');

    // Remove any existing listeners to avoid duplicates
    filesDiv.removeEventListener('click', handleDeleteClick);

    // Add event delegation listener
    filesDiv.addEventListener('click', handleDeleteClick);
}

// Handle delete button clicks
function handleDeleteClick(event) {
    if (event.target.classList.contains('delete-btn')) {
        const filename = event.target.getAttribute('data-filename');
        if (filename) {
            deleteFile(filename);
        }
    }
}

// Delete file function
function deleteFile(filename) {
    if (!confirm('Are you sure you want to delete "' + filename + '"?')) {
        return;
    }

    fetch('/api/files/' + encodeURIComponent(filename), {
        method: 'DELETE'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showResult('✓ File "' + filename + '" deleted successfully.', 'success');
                loadFilesList();
            } else {
                showResult('✗ Delete failed: ' + (data.error || 'Unknown error'), 'danger');
            }
        })
        .catch(error => {
            showResult('✗ Delete error: ' + error.message, 'danger');
        });
}

// Load files list on page load
document.addEventListener('DOMContentLoaded', function () {
    loadFilesList();
});
