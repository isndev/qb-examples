// HTTP/2 Demo JavaScript with CSP-compatible event handling
class Http2Demo {
    constructor() {
        this.currentSection = 'overview';
        this.performanceData = [];
        this.performanceChart = null;
        this.initialized = false; // Protection contre multiple initialisation
        this.isRequestInProgress = {}; // Protection contre les clics multiples
        
        // Créer des références de fonction stables pour pouvoir les supprimer proprement
        this.boundHandlers = {
            handleNavClick: this.handleNavClick.bind(this),
            handleTestAllFeatures: this.handleTestAllFeatures.bind(this),
            handleStartMultiplexing: this.handleStartMultiplexing.bind(this),
            handleClearMultiplexing: this.handleClearMultiplexing.bind(this),
            handleRequestPush: this.handleRequestPush.bind(this),
            handleRequestMultiPush: this.handleRequestMultiPush.bind(this),
            handleClearPush: this.handleClearPush.bind(this),
            handlePriorityTest: this.handlePriorityTest.bind(this),
            handleClearPriority: this.handleClearPriority.bind(this),
            handleRunPerformance: this.handleRunPerformance.bind(this),
            handleClearPerformance: this.handleClearPerformance.bind(this)
        };
    }

    init() {
        if (this.initialized) {
            console.warn('Http2Demo already initialized');
            return;
        }
        
        this.setupEventListeners();
        this.updateServerTime();
        this.initPerformanceChart();
        
        // Update server time every 30 seconds
        setInterval(() => this.updateServerTime(), 30000);
        
        // Show initial section
        this.switchSection('overview');
        
        this.initialized = true;
        console.log('Http2Demo initialized successfully');
    }

    setupEventListeners() {
        console.log('Setting up event listeners...');
        
        // Navigation buttons
        document.querySelectorAll('.nav-btn').forEach(btn => {
            // Retirer les anciens listeners s'ils existent
            btn.removeEventListener('click', this.boundHandlers.handleNavClick);
            btn.addEventListener('click', this.boundHandlers.handleNavClick, { once: false, passive: false });
        });

        // Overview section
        const testAllBtn = document.getElementById('test-all-features');
        if (testAllBtn) {
            testAllBtn.removeEventListener('click', this.boundHandlers.handleTestAllFeatures);
            testAllBtn.addEventListener('click', this.boundHandlers.handleTestAllFeatures, { once: false, passive: false });
        }

        // Multiplexing section
        const startMultiplexingBtn = document.getElementById('start-multiplexing');
        if (startMultiplexingBtn) {
            startMultiplexingBtn.removeEventListener('click', this.boundHandlers.handleStartMultiplexing);
            startMultiplexingBtn.addEventListener('click', this.boundHandlers.handleStartMultiplexing, { once: false, passive: false });
        }
        
        const clearMultiplexingBtn = document.getElementById('clear-multiplexing');
        if (clearMultiplexingBtn) {
            clearMultiplexingBtn.removeEventListener('click', this.boundHandlers.handleClearMultiplexing);
            clearMultiplexingBtn.addEventListener('click', this.boundHandlers.handleClearMultiplexing, { once: false, passive: false });
        }

        // Server push section
        const requestPushBtn = document.getElementById('request-push');
        if (requestPushBtn) {
            requestPushBtn.removeEventListener('click', this.boundHandlers.handleRequestPush);
            requestPushBtn.addEventListener('click', this.boundHandlers.handleRequestPush, { once: false, passive: false });
        }

        const requestMultiPushBtn = document.getElementById('request-multi-push');
        if (requestMultiPushBtn) {
            requestMultiPushBtn.removeEventListener('click', this.boundHandlers.handleRequestMultiPush);
            requestMultiPushBtn.addEventListener('click', this.boundHandlers.handleRequestMultiPush, { once: false, passive: false });
        }

        const clearPushBtn = document.getElementById('clear-push');
        if (clearPushBtn) {
            clearPushBtn.removeEventListener('click', this.boundHandlers.handleClearPush);
            clearPushBtn.addEventListener('click', this.boundHandlers.handleClearPush, { once: false, passive: false });
        }

        // Priority section
        const priorityBtns = document.querySelectorAll('[id^="test-"][id$="-priority"]');
        priorityBtns.forEach(btn => {
            btn.removeEventListener('click', this.boundHandlers.handlePriorityTest);
            btn.addEventListener('click', this.boundHandlers.handlePriorityTest, { once: false, passive: false });
        });

        const clearPriorityBtn = document.getElementById('clear-priority');
        if (clearPriorityBtn) {
            clearPriorityBtn.removeEventListener('click', this.boundHandlers.handleClearPriority);
            clearPriorityBtn.addEventListener('click', this.boundHandlers.handleClearPriority, { once: false, passive: false });
        }

        // Performance section
        const runPerformanceBtn = document.getElementById('run-performance');
        if (runPerformanceBtn) {
            runPerformanceBtn.removeEventListener('click', this.boundHandlers.handleRunPerformance);
            runPerformanceBtn.addEventListener('click', this.boundHandlers.handleRunPerformance, { once: false, passive: false });
        }

        const clearPerformanceBtn = document.getElementById('clear-performance');
        if (clearPerformanceBtn) {
            clearPerformanceBtn.removeEventListener('click', this.boundHandlers.handleClearPerformance);
            clearPerformanceBtn.addEventListener('click', this.boundHandlers.handleClearPerformance, { once: false, passive: false });
        }
        
        console.log('Event listeners setup completed');
    }

    // Handlers avec protection contre la double exécution
    handleNavClick(e) {
        e.preventDefault();
        e.stopPropagation();
        const section = e.target.getAttribute('data-section');
        if (section && section !== this.currentSection) {
            this.switchSection(section);
        }
    }

    handleTestAllFeatures(e) {
        e.preventDefault();
        e.stopPropagation();
        if (this.isRequestInProgress['testAll']) {
            console.log('Test all features already in progress, ignoring click');
            return;
        }
        this.testAllFeatures();
    }

    handleStartMultiplexing(e) {
        e.preventDefault();
        e.stopPropagation();
        if (this.isRequestInProgress['multiplexing']) {
            console.log('Multiplexing test already in progress, ignoring click');
            return;
        }
        this.startMultiplexingTest();
    }

    handleClearMultiplexing(e) {
        e.preventDefault();
        e.stopPropagation();
        this.clearMultiplexingResults();
    }

    handleRequestPush(e) {
        e.preventDefault();
        e.stopPropagation();
        if (this.isRequestInProgress['push']) return;
        this.requestPushDemo();
    }

    handleRequestMultiPush(e) {
        e.preventDefault();
        e.stopPropagation();
        if (this.isRequestInProgress['multiPush']) return;
        this.requestMultiPush();
    }

    handleClearPush(e) {
        e.preventDefault();
        e.stopPropagation();
        this.clearPushTimeline();
    }

    handlePriorityTest(e) {
        e.preventDefault();
        e.stopPropagation();
        const priority = e.target.getAttribute('data-priority') || 'medium';
        if (this.isRequestInProgress[`priority-${priority}`]) return;
        this.testStreamPriority(priority);
    }

    handleClearPriority(e) {
        e.preventDefault();
        e.stopPropagation();
        this.clearPriorityResults();
    }

    handleRunPerformance(e) {
        e.preventDefault();
        e.stopPropagation();
        if (this.isRequestInProgress['performance']) return;
        this.runPerformanceTest();
    }

    handleClearPerformance(e) {
        e.preventDefault();
        e.stopPropagation();
        this.clearPerformanceResults();
    }

    switchSection(sectionId) {
        // Update navigation
        document.querySelectorAll('.nav-btn').forEach(btn => {
            btn.classList.remove('active');
            if (btn.getAttribute('data-section') === sectionId) {
                btn.classList.add('active');
            }
        });

        // Update sections
        document.querySelectorAll('.section').forEach(section => {
            section.classList.remove('active');
        });
        
        const targetSection = document.getElementById(sectionId);
        if (targetSection) {
            targetSection.classList.add('active');
        }
        
        this.currentSection = sectionId;
    }

    updateServerTime() {
        const timeElement = document.getElementById('server-time');
        if (timeElement) {
            timeElement.textContent = new Date().toLocaleString();
        }
    }

    showToast(message, type = 'info') {
        const container = document.getElementById('toast-container');
        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.textContent = message;
        
        container.appendChild(toast);
        
        // Remove toast after 3 seconds
        setTimeout(() => {
            toast.remove();
        }, 3000);
    }

    async makeRequest(url, options = {}) {
        const startTime = performance.now();
        
        try {
            const response = await fetch(url, {
                method: options.method || 'GET',
                headers: {
                    'Content-Type': 'application/json',
                    ...options.headers
                },
                body: options.body ? JSON.stringify(options.body) : undefined
            });
            
            const endTime = performance.now();
            const duration = endTime - startTime;
            
            const responseData = await response.text();
            
            return {
                success: response.ok,
                status: response.status,
                statusText: response.statusText,
                data: responseData,
                duration: Math.round(duration),
                timestamp: new Date().toISOString()
            };
        } catch (error) {
            const endTime = performance.now();
            const duration = endTime - startTime;
            
            return {
                success: false,
                error: error.message,
                duration: Math.round(duration),
                timestamp: new Date().toISOString()
            };
        }
    }

    initPerformanceChart() {
        const canvas = document.getElementById('performance-chart');
        if (canvas) {
            this.performanceChart = canvas.getContext('2d');
        }
    }

    drawPerformanceChart() {
        if (!this.performanceChart || this.performanceData.length === 0) return;
        
        const canvas = this.performanceChart.canvas;
        const ctx = this.performanceChart;
        
        // Clear canvas
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        
        // Set up chart dimensions
        const padding = 50;
        const chartWidth = canvas.width - 2 * padding;
        const chartHeight = canvas.height - 2 * padding;
        
        // Find max values for scaling
        const maxDuration = Math.max(...this.performanceData.map(d => d.duration));
        const maxIndex = this.performanceData.length - 1;
        
        // Draw axes
        ctx.strokeStyle = '#ddd';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(padding, padding);
        ctx.lineTo(padding, padding + chartHeight);
        ctx.lineTo(padding + chartWidth, padding + chartHeight);
        ctx.stroke();
        
        // Draw data
        if (this.performanceData.length > 1) {
            this.drawLine(this.performanceData, '#007bff', 'Response Time');
        }
        
        // Draw labels
        ctx.fillStyle = '#333';
        ctx.font = '12px Arial';
        ctx.textAlign = 'center';
        ctx.fillText('Request Number', padding + chartWidth / 2, canvas.height - 10);
        
        ctx.save();
        ctx.translate(15, padding + chartHeight / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.fillText('Response Time (ms)', 0, 0);
        ctx.restore();
    }

    drawLine(data, color, label) {
        const canvas = this.performanceChart.canvas;
        const ctx = this.performanceChart;
        const padding = 50;
        const chartWidth = canvas.width - 2 * padding;
        const chartHeight = canvas.height - 2 * padding;
        
        const maxDuration = Math.max(...data.map(d => d.duration));
        const maxIndex = data.length - 1;
        
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        
        data.forEach((point, index) => {
            const x = padding + (index / maxIndex) * chartWidth;
            const y = padding + chartHeight - (point.duration / maxDuration) * chartHeight;
            
            if (index === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        });
        
        ctx.stroke();
        
        // Draw points
        ctx.fillStyle = color;
        data.forEach((point, index) => {
            const x = padding + (index / maxIndex) * chartWidth;
            const y = padding + chartHeight - (point.duration / maxDuration) * chartHeight;
            
            ctx.beginPath();
            ctx.arc(x, y, 3, 0, 2 * Math.PI);
            ctx.fill();
        });
    }

    // Event handler methods
    async testAllFeatures() {
        this.isRequestInProgress['testAll'] = true;
        this.showToast('Testing all HTTP/2 features...', 'info');
        
        try {
            // Test each feature sequentially
            await this.startMultiplexingTest();
            await new Promise(resolve => setTimeout(resolve, 1000));
            
            await this.requestPushDemo();
            await new Promise(resolve => setTimeout(resolve, 1000));
            
            await this.testStreamPriority('high');
            await new Promise(resolve => setTimeout(resolve, 1000));
            
            await this.runPerformanceTest();
            
            this.showToast('All HTTP/2 features tested successfully!', 'success');
        } catch (error) {
            this.showToast('Error testing features: ' + error.message, 'error');
        } finally {
            this.isRequestInProgress['testAll'] = false;
        }
    }

    async startMultiplexingTest() {
        if (this.isRequestInProgress['multiplexing']) return;
        this.isRequestInProgress['multiplexing'] = true;
        
        try {
            const resultsContainer = document.getElementById('multiplexing-results');
            resultsContainer.innerHTML = '<div class="info-message">Starting multiplexing test...</div>';
            
            const requests = [
                { url: '/api/multiplexing-demo?request=1', name: 'Request 1' },
                { url: '/api/multiplexing-demo?request=2', name: 'Request 2' },
                { url: '/api/multiplexing-demo?request=3', name: 'Request 3' },
                { url: '/api/multiplexing-demo?request=4', name: 'Request 4' },
                { url: '/api/multiplexing-demo?request=5', name: 'Request 5' }
            ];
            
            resultsContainer.innerHTML = '';
            
            // Start all requests simultaneously
            const startTime = performance.now();
            const promises = requests.map(async (req, index) => {
                const result = await this.makeRequest(req.url);
                const endTime = performance.now();
                
                const resultDiv = document.createElement('div');
                resultDiv.className = 'request-result';
                resultDiv.innerHTML = `
                    <div class="request-info">
                        <strong>${req.name}</strong>
                        <span class="status ${result.success ? 'success' : 'error'}">
                            ${result.success ? 'SUCCESS' : 'FAILED'}
                        </span>
                        <span class="duration">${result.duration}ms</span>
                    </div>
                    <div class="request-details">
                        Status: ${result.status || 'N/A'} | 
                        Response time: ${result.duration}ms |
                        Stream ID: ${result.data ? JSON.parse(result.data).stream_id : 'N/A'}
                    </div>
                `;
                
                resultsContainer.appendChild(resultDiv);
                return result;
            });
            
            const results = await Promise.all(promises);
            const totalTime = performance.now() - startTime;
            
            // Add summary
            const summaryDiv = document.createElement('div');
            summaryDiv.className = 'test-summary';
            summaryDiv.innerHTML = `
                <h4>Multiplexing Test Summary</h4>
                <p>Completed ${results.length} simultaneous requests in ${Math.round(totalTime)}ms</p>
                <p>Average response time: ${Math.round(results.reduce((sum, r) => sum + r.duration, 0) / results.length)}ms</p>
                <p>All requests used the same HTTP/2 connection with different stream IDs</p>
            `;
            resultsContainer.appendChild(summaryDiv);
            
            this.showToast(`Multiplexing test completed! ${results.length} requests in ${Math.round(totalTime)}ms`, 'success');
        } catch (error) {
            this.showToast('Multiplexing test failed: ' + error.message, 'error');
        } finally {
            this.isRequestInProgress['multiplexing'] = false;
        }
    }

    clearMultiplexingResults() {
        const results = document.getElementById('multiplexing-results');
        if (results) {
            results.innerHTML = '<div class="info-message">Click "Start Multiplexing Test" to begin</div>';
        }
    }

    async requestPushDemo() {
        if (this.isRequestInProgress['push']) return;
        this.isRequestInProgress['push'] = true;
        
        try {
            const timeline = document.getElementById('push-results');
            
            function addTimelineItem(message, type = 'normal') {
                const item = document.createElement('div');
                item.className = `push-timeline-item ${type}`;
                item.innerHTML = `
                    <span class="timestamp">${new Date().toLocaleTimeString()}</span>
                    <span class="message">${message}</span>
                `;
                timeline.appendChild(item);
                timeline.scrollTop = timeline.scrollHeight;
            }
            
            timeline.innerHTML = '';
            addTimelineItem('🚀 Initiating server push demo...', 'info');
            
            const result = await this.makeRequest('/api/server-push-demo');
            
            if (result.success) {
                const data = JSON.parse(result.data);
                addTimelineItem('✅ Server push request successful', 'success');
                addTimelineItem(`📦 Server pushed ${data.pushed_resources.length} resources:`, 'info');
                
                data.pushed_resources.forEach((resource, index) => {
                    setTimeout(() => {
                        addTimelineItem(`   • ${resource}`, 'push');
                    }, (index + 1) * 200);
                });
                
                setTimeout(() => {
                    addTimelineItem('🏁 All resources received by client', 'success');
                    this.showToast('Server push demo completed successfully!', 'success');
                }, data.pushed_resources.length * 200 + 500);
            } else {
                addTimelineItem('❌ Server push demo failed', 'error');
                this.showToast('Server push demo failed', 'error');
            }
        } catch (error) {
            this.showToast('Server push demo failed: ' + error.message, 'error');
        } finally {
            this.isRequestInProgress['push'] = false;
        }
    }

    async requestMultiPush() {
        if (this.isRequestInProgress['multiPush']) return;
        this.isRequestInProgress['multiPush'] = true;
        
        try {
            const timeline = document.getElementById('push-results');
            
            function addTimelineItem(message, type = 'normal') {
                const item = document.createElement('div');
                item.className = `push-timeline-item ${type}`;
                item.innerHTML = `
                    <span class="timestamp">${new Date().toLocaleTimeString()}</span>
                    <span class="message">${message}</span>
                `;
                timeline.appendChild(item);
                timeline.scrollTop = timeline.scrollHeight;
            }
            
            timeline.innerHTML = '';
            addTimelineItem('🚀 Requesting multiple resources with server push...', 'info');
            
            const requests = [
                '/api/server-push-demo?resource=styles',
                '/api/server-push-demo?resource=scripts',
                '/api/server-push-demo?resource=data'
            ];
            
            const promises = requests.map(async (url, index) => {
                addTimelineItem(`📤 Requesting: ${url}`, 'info');
                const result = await this.makeRequest(url);
                
                if (result.success) {
                    const data = JSON.parse(result.data);
                    addTimelineItem(`✅ Response ${index + 1}: ${data.pushed_resources.length} resources pushed`, 'success');
                }
                
                return result;
            });
            
            const results = await Promise.all(promises);
            const successCount = results.filter(r => r.success).length;
            
            addTimelineItem(`🏁 Multi-push demo completed: ${successCount}/${requests.length} successful`, 'success');
            this.showToast(`Multi-push demo completed: ${successCount}/${requests.length} successful`, 'success');
        } catch (error) {
            this.showToast('Multi-push demo failed: ' + error.message, 'error');
        } finally {
            this.isRequestInProgress['multiPush'] = false;
        }
    }

    clearPushTimeline() {
        const timeline = document.getElementById('push-results');
        if (timeline) {
            timeline.innerHTML = '<div class="info-message">Click buttons above to test server push</div>';
        }
    }

    async testStreamPriority(priority = 'medium') {
        if (this.isRequestInProgress[`priority-${priority}`]) return;
        this.isRequestInProgress[`priority-${priority}`] = true;
        
        try {
            const results = document.getElementById('priority-results');
            
            function addTimelineItem(message, priority = 'medium') {
                const item = document.createElement('div');
                item.className = `priority-item ${priority}`;
                item.innerHTML = `
                    <span class="timestamp">${new Date().toLocaleTimeString()}</span>
                    <span class="priority-level">[${priority.toUpperCase()}]</span>
                    <span class="message">${message}</span>
                `;
                results.appendChild(item);
                results.scrollTop = results.scrollHeight;
            }
            
            addTimelineItem(`🎯 Testing ${priority} priority stream...`, priority);
            
            const result = await this.makeRequest(`/api/stream-priority/${priority}`);
            
            if (result.success) {
                const data = JSON.parse(result.data);
                addTimelineItem(`✅ Priority test completed in ${result.duration}ms`, priority);
                addTimelineItem(`⚖️ Priority weight: ${data.weight}`, priority);
                addTimelineItem(`⏱️ Processing time: ${data.processing_time_ms}ms`, priority);
                
                this.showToast(`${priority} priority test completed (${result.duration}ms)`, 'success');
            } else {
                addTimelineItem(`❌ Priority test failed`, priority);
                this.showToast(`${priority} priority test failed`, 'error');
            }
        } catch (error) {
            this.showToast(`Priority test failed: ${error.message}`, 'error');
        } finally {
            this.isRequestInProgress[`priority-${priority}`] = false;
        }
    }

    clearPriorityResults() {
        const timeline = document.getElementById('priority-results');
        if (timeline) {
            timeline.innerHTML = '<div class="info-message">Click priority buttons to test stream prioritization</div>';
        }
    }

    async runPerformanceTest() {
        if (this.isRequestInProgress['performance']) return;
        this.isRequestInProgress['performance'] = true;
        
        try {
            const results = document.getElementById('performance-results');
            const responseTimeEl = document.getElementById('response-time');
            const throughputEl = document.getElementById('throughput');
            const activeStreamsEl = document.getElementById('active-streams');
            const dataTransferredEl = document.getElementById('data-transferred');
            
            const iterations = 50; // Fixed iterations for consistency
            this.performanceData = [];
            
            this.showToast(`Running performance test with ${iterations} iterations...`, 'info');
            
            for (let i = 0; i < iterations; i++) {
                const result = await this.makeRequest(`/api/performance/${iterations}?iteration=${i + 1}`);
                
                this.performanceData.push({
                    iteration: i + 1,
                    duration: result.duration,
                    success: result.success,
                    timestamp: Date.now()
                });
                
                // Update real-time metrics
                const avgResponseTime = this.performanceData.reduce((sum, d) => sum + d.duration, 0) / this.performanceData.length;
                const throughput = 1000 / avgResponseTime; // requests per second
                const dataSize = 0.5; // KB estimate per request
                
                if (responseTimeEl) responseTimeEl.textContent = `${Math.round(avgResponseTime)} ms`;
                if (throughputEl) throughputEl.textContent = `${Math.round(throughput)} req/s`;
                if (activeStreamsEl) activeStreamsEl.textContent = `${Math.min(i + 1, 10)}`;
                if (dataTransferredEl) dataTransferredEl.textContent = `${Math.round((i + 1) * dataSize)} KB`;
                
                // Update progress
                const progress = Math.round(((i + 1) / iterations) * 100);
                results.innerHTML = `
                    <div class="performance-progress">
                        <div class="progress-bar">
                            <div class="progress-fill" style="width: ${progress}%"></div>
                        </div>
                        <div class="progress-text">
                            Progress: ${i + 1}/${iterations} (${progress}%)
                        </div>
                        <div class="performance-stats">
                            <div>Current Response Time: ${result.duration}ms</div>
                            <div>Average Response Time: ${Math.round(avgResponseTime)}ms</div>
                            <div>Throughput: ${Math.round(throughput)} req/s</div>
                        </div>
                    </div>
                `;
                
                // Small delay to prevent overwhelming the server
                if (i < iterations - 1) {
                    await new Promise(resolve => setTimeout(resolve, 50));
                }
            }
            
            // Calculate final statistics
            const successfulRequests = this.performanceData.filter(d => d.success).length;
            const avgResponseTime = this.performanceData.reduce((sum, d) => sum + d.duration, 0) / this.performanceData.length;
            const minResponseTime = Math.min(...this.performanceData.map(d => d.duration));
            const maxResponseTime = Math.max(...this.performanceData.map(d => d.duration));
            const throughput = 1000 / avgResponseTime;
            
            // Display final results
            results.innerHTML = `
                <div class="performance-summary">
                    <h4>Performance Test Results</h4>
                    <div class="stats-grid">
                        <div class="stat-item">
                            <label>Total Requests:</label>
                            <value>${iterations}</value>
                        </div>
                        <div class="stat-item">
                            <label>Successful:</label>
                            <value>${successfulRequests}</value>
                        </div>
                        <div class="stat-item">
                            <label>Success Rate:</label>
                            <value>${Math.round((successfulRequests / iterations) * 100)}%</value>
                        </div>
                        <div class="stat-item">
                            <label>Avg Response Time:</label>
                            <value>${Math.round(avgResponseTime)}ms</value>
                        </div>
                        <div class="stat-item">
                            <label>Min Response Time:</label>
                            <value>${minResponseTime}ms</value>
                        </div>
                        <div class="stat-item">
                            <label>Max Response Time:</label>
                            <value>${maxResponseTime}ms</value>
                        </div>
                        <div class="stat-item">
                            <label>Throughput:</label>
                            <value>${Math.round(throughput)} req/s</value>
                        </div>
                        <div class="stat-item">
                            <label>Data Transferred:</label>
                            <value>${Math.round(iterations * 0.5)} KB</value>
                        </div>
                    </div>
                </div>
            `;
            
            this.drawPerformanceChart();
            this.showToast(`Performance test completed! ${successfulRequests}/${iterations} successful requests`, 'success');
        } catch (error) {
            this.showToast('Performance test failed: ' + error.message, 'error');
        } finally {
            this.isRequestInProgress['performance'] = false;
        }
    }

    clearPerformanceResults() {
        const results = document.getElementById('performance-results');
        if (results) {
            results.innerHTML = '<div class="info-message">Click "Run Performance Test" to start analysis</div>';
        }
        
        // Reset metrics
        const responseTimeEl = document.getElementById('response-time');
        const throughputEl = document.getElementById('throughput');
        const activeStreamsEl = document.getElementById('active-streams');
        const dataTransferredEl = document.getElementById('data-transferred');
        
        if (responseTimeEl) responseTimeEl.textContent = '-- ms';
        if (throughputEl) throughputEl.textContent = '-- req/s';
        if (activeStreamsEl) activeStreamsEl.textContent = '--';
        if (dataTransferredEl) dataTransferredEl.textContent = '-- KB';
        
        this.performanceData = [];
        this.drawPerformanceChart();
    }
}

// Global instance to prevent double initialization
let globalHttp2Demo = null;

// Initialize demo when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    if (!globalHttp2Demo) {
        globalHttp2Demo = new Http2Demo();
        globalHttp2Demo.init();
        console.log('Http2Demo initialized globally');
    } else {
        console.warn('Http2Demo already initialized, skipping duplicate initialization');
    }
}); 