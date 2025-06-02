// QB HTTP Framework - Client-side utilities

document.addEventListener('DOMContentLoaded', function() {
    console.log('QB HTTP Static File Server - JavaScript loaded');
    
    // Add some interactive features
    enhanceNavigation();
    addClickEffects();
    showPageInfo();
});

function enhanceNavigation() {
    // Add smooth scrolling for anchor links
    const links = document.querySelectorAll('a[href^="#"]');
    links.forEach(link => {
        link.addEventListener('click', function(e) {
            e.preventDefault();
            const target = document.querySelector(this.getAttribute('href'));
            if (target) {
                target.scrollIntoView({
                    behavior: 'smooth'
                });
            }
        });
    });
}

function addClickEffects() {
    // Add simple click effect to buttons without any dynamic CSS
    const buttons = document.querySelectorAll('.btn, button');
    buttons.forEach(button => {
        button.addEventListener('click', function(e) {
            // Add a simple class-based effect
            this.classList.add('clicked');
            
            setTimeout(() => {
                this.classList.remove('clicked');
            }, 200);
        });
        
        // Add hover class for better UX
        button.addEventListener('mouseenter', function() {
            this.classList.add('hover');
        });
        
        button.addEventListener('mouseleave', function() {
            this.classList.remove('hover');
        });
    });
}

function showPageInfo() {
    // Display current page information
    const now = new Date();
    const pageInfo = {
        url: window.location.href,
        userAgent: navigator.userAgent,
        timestamp: now.toISOString(),
        localTime: now.toLocaleString()
    };
    
    console.log('Page Information:', pageInfo);
    
    // Add debug info to footer if needed (without dynamic CSS)
    if (window.location.search.includes('debug=true')) {
        const footer = document.querySelector('footer');
        if (footer) {
            const debugInfo = document.createElement('div');
            debugInfo.className = 'debug-info';
            debugInfo.innerHTML = '<strong>Debug Info:</strong><br>' +
                'URL: ' + pageInfo.url + '<br>' +
                'Time: ' + pageInfo.localTime + '<br>' +
                'User Agent: ' + pageInfo.userAgent;
            footer.appendChild(debugInfo);
        }
    }
}

// Utility functions for API interactions
const QBHttpUtils = {
    async apiRequest(endpoint, options = {}) {
        try {
            const response = await fetch(endpoint, {
                headers: {
                    'Content-Type': 'application/json',
                    ...options.headers
                },
                ...options
            });
            
            const data = await response.json();
            
            if (!response.ok) {
                throw new Error(data.error || 'HTTP ' + response.status);
            }
            
            return data;
        } catch (error) {
            console.error('API Request failed:', error);
            throw error;
        }
    },

    async listFiles() {
        return this.apiRequest('/api/files');
    },

    async getFileMetadata(filename) {
        return this.apiRequest('/api/files/' + encodeURIComponent(filename));
    },

    async deleteFile(filename) {
        return this.apiRequest('/api/files/' + encodeURIComponent(filename), {
            method: 'DELETE'
        });
    },

    async updateFileMetadata(filename, metadata) {
        return this.apiRequest('/api/files/' + encodeURIComponent(filename) + '/metadata', {
            method: 'PUT',
            body: JSON.stringify(metadata)
        });
    },

    formatFileSize(bytes) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024;
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    },

    formatDate(timestamp) {
        return new Date(timestamp * 1000).toLocaleString();
    }
};

// Make utilities available globally
window.QBHttpUtils = QBHttpUtils; 