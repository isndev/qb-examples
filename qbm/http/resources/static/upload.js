// Upload form handling
document.getElementById('uploadForm').addEventListener('submit', function(e) {
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
document.getElementById('clearBtn').addEventListener('click', function() {
    document.getElementById('uploadForm').reset();
});

// Refresh files button
document.getElementById('refreshBtn').addEventListener('click', function() {
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
        console.log('API Response:', data); // Debug
        
        if (!data.files || data.files.length === 0) {
            filesDiv.innerHTML = '<p><em>No files uploaded yet.</em></p>';
            return;
        }
        
        let html = '<table><thead><tr><th>Filename</th><th>Size</th><th>Type</th><th>Uploaded</th><th>Actions</th></tr></thead><tbody>';
        
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
            
            const descriptionHtml = description ? '<br><small>' + description + '</small>' : '';
            const tagsHtml = tags.length > 0 ? '<br><small>Tags: ' + tags.join(', ') + '</small>' : '';
            
            html += '<tr><td><strong>' + file.filename + '</strong>' + descriptionHtml + tagsHtml + '</td>';
            html += '<td>' + fileSize + '</td>';
            html += '<td>' + mimeType + '</td>';
            html += '<td>' + uploadDate + '</td>';
            html += '<td>';
            html += '<a href="/uploads/' + file.filename + '" class="btn" target="_blank">Download</a> ';
            html += '<button class="btn btn-danger delete-btn" data-filename="' + file.filename + '">Delete</button>';
            html += '</td></tr>';
        });
        
        html += '</tbody></table>';
        filesDiv.innerHTML = html;
        
        // Attach event listeners to delete buttons (event delegation)
        attachDeleteHandlers();
        
    })
    .catch(error => {
        console.error('Load files error:', error);
        filesDiv.innerHTML = '<p class="alert alert-danger">Error loading files: ' + error.message + '</p>';
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
document.addEventListener('DOMContentLoaded', function() {
    loadFilesList();
}); 