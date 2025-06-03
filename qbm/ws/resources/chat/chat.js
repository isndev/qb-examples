class ChatClient {
    constructor() {
        this.ws = null;
        this.username = 'User';
        this.connected = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        
        // Get DOM elements
        this.messagesContainer = document.getElementById('chatMessages');
        this.messageInput = document.getElementById('messageInput');
        this.sendButton = document.getElementById('sendButton');
        this.usernameInput = document.getElementById('usernameInput');
        this.connectionStatus = document.getElementById('connectionStatus');
        
        this.init();
    }
    
    init() {
        // Setup event listeners
        this.sendButton.addEventListener('click', () => this.sendMessage());
        this.messageInput.addEventListener('keypress', (e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                this.sendMessage();
            }
        });
        
        this.usernameInput.addEventListener('change', (e) => {
            this.username = e.target.value.trim() || 'User';
            // Send username update to server if connected
            if (this.connected) {
                this.sendUsernameUpdate();
            }
        });
        
        // Connect to WebSocket
        this.connect();
        
        // Add some helpful info to console
        console.log('QB WebSocket Chat Client initialized');
        console.log('Expected WebSocket endpoint: ws://localhost:8080/ws');
    }
    
    connect() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.host}/ws`;
        
        this.updateConnectionStatus('Connecting...', 'loading');
        
        try {
            this.ws = new WebSocket(wsUrl);
            
            this.ws.onopen = () => {
                this.connected = true;
                this.reconnectAttempts = 0;
                this.updateConnectionStatus('Connected', 'connected');
                this.sendButton.disabled = false;
                this.messageInput.disabled = false;
                this.addSystemMessage('Connected to chat server');
                
                // Send initial user join notification
                this.sendUserJoined();
            };
            
            this.ws.onmessage = (event) => {
                this.handleMessage(event.data);
            };
            
            this.ws.onclose = (event) => {
                this.connected = false;
                this.updateConnectionStatus('Disconnected', 'disconnected');
                this.sendButton.disabled = true;
                this.messageInput.disabled = true;
                
                if (event.wasClean) {
                    this.addSystemMessage('Connection closed normally');
                } else {
                    this.addSystemMessage(`Connection lost (code: ${event.code})`);
                    this.attemptReconnect();
                }
            };
            
            this.ws.onerror = (error) => {
                console.error('WebSocket error:', error);
                this.addSystemMessage('Connection error occurred');
                this.updateConnectionStatus('Connection Error', 'disconnected');
            };
            
        } catch (error) {
            console.error('Failed to create WebSocket connection:', error);
            this.updateConnectionStatus('Connection Failed', 'disconnected');
            this.addSystemMessage('Failed to connect to chat server');
            this.addSystemMessage('Make sure the WebSocket server is running on /ws endpoint');
        }
    }
    
    attemptReconnect() {
        if (this.reconnectAttempts < this.maxReconnectAttempts) {
            this.reconnectAttempts++;
            const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 10000); // Exponential backoff
            
            this.addSystemMessage(`Reconnecting in ${delay/1000} seconds... (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);
            
            setTimeout(() => {
                if (!this.connected) {
                    this.connect();
                }
            }, delay);
        } else {
            this.addSystemMessage('Max reconnection attempts reached. Please refresh the page.');
        }
    }
    
    sendMessage() {
        const message = this.messageInput.value.trim();
        if (!message || !this.connected) {
            return;
        }
        
        const messageObj = {
            type: 'message',
            username: this.username,
            message: message,
            timestamp: Date.now()
        };
        
        try {
            this.ws.send(JSON.stringify(messageObj));
            this.messageInput.value = '';
            
            // Add our own message to the chat (optimistic update)
            this.addMessage(messageObj, true);
        } catch (error) {
            console.error('Failed to send message:', error);
            this.addSystemMessage('Failed to send message');
        }
    }
    
    sendUsernameUpdate() {
        if (!this.connected) return;
        
        const updateObj = {
            type: 'username_update',
            username: this.username,
            timestamp: Date.now()
        };
        
        try {
            this.ws.send(JSON.stringify(updateObj));
        } catch (error) {
            console.error('Failed to send username update:', error);
        }
    }
    
    sendUserJoined() {
        if (!this.connected) return;
        
        const joinObj = {
            type: 'user_joined',
            username: this.username,
            timestamp: Date.now()
        };
        
        try {
            this.ws.send(JSON.stringify(joinObj));
        } catch (error) {
            console.error('Failed to send user joined:', error);
        }
    }
    
    handleMessage(data) {
        try {
            const messageObj = JSON.parse(data);
            
            switch (messageObj.type) {
                case 'message':
                    // Only add if it's not our own message (avoid duplicates)
                    if (messageObj.username !== this.username) {
                        this.addMessage(messageObj, false);
                    }
                    break;
                case 'user_joined':
                    if (messageObj.username !== this.username) {
                        this.addSystemMessage(`${messageObj.username} joined the chat`);
                    }
                    break;
                case 'user_left':
                    this.addSystemMessage(`${messageObj.username} left the chat`);
                    break;
                case 'username_changed':
                    this.addSystemMessage(`${messageObj.old_username} is now known as ${messageObj.new_username}`);
                    break;
                case 'system':
                    this.addSystemMessage(messageObj.message);
                    break;
                case 'error':
                    this.addSystemMessage(`Error: ${messageObj.message}`, 'error');
                    break;
                default:
                    console.warn('Unknown message type:', messageObj.type);
                    this.addSystemMessage(`Unknown message: ${data}`);
            }
        } catch (error) {
            console.error('Failed to parse message:', error);
            // If it's not JSON, treat as plain text from server
            this.addSystemMessage(`Server: ${data}`);
        }
    }
    
    addMessage(messageObj, isOwnMessage = false) {
        const messageDiv = document.createElement('div');
        messageDiv.className = `message ${isOwnMessage ? 'own' : 'other'}`;
        
        const time = new Date(messageObj.timestamp || Date.now()).toLocaleTimeString();
        
        messageDiv.innerHTML = `
            ${!isOwnMessage ? `<div class="message-header">${this.escapeHtml(messageObj.username)}</div>` : ''}
            <span class="message-content">${this.escapeHtml(messageObj.message)}</span>
            <span class="message-time">${time}</span>
        `;
        
        this.messagesContainer.appendChild(messageDiv);
        this.scrollToBottom();
        
        // Limit message history (keep last 100 messages)
        this.cleanupMessages();
    }
    
    addSystemMessage(message, type = 'info') {
        const messageDiv = document.createElement('div');
        messageDiv.className = `message system ${type}`;
        
        const time = new Date().toLocaleTimeString();
        
        messageDiv.innerHTML = `
            <span class="message-content">${this.escapeHtml(message)}</span>
            <span class="message-time">${time}</span>
        `;
        
        this.messagesContainer.appendChild(messageDiv);
        this.scrollToBottom();
        this.cleanupMessages();
    }
    
    cleanupMessages() {
        const messages = this.messagesContainer.children;
        const maxMessages = 100;
        
        while (messages.length > maxMessages) {
            this.messagesContainer.removeChild(messages[0]);
        }
    }
    
    updateConnectionStatus(text, className = '') {
        this.connectionStatus.textContent = text;
        this.connectionStatus.className = `connection-status ${className}`;
    }
    
    scrollToBottom() {
        this.messagesContainer.scrollTop = this.messagesContainer.scrollHeight;
    }
    
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
    
    // Public method to send a message programmatically (for testing)
    sendTestMessage(message) {
        this.messageInput.value = message;
        this.sendMessage();
    }
    
    // Public method to change username programmatically  
    changeUsername(newUsername) {
        this.usernameInput.value = newUsername;
        this.username = newUsername;
        this.sendUsernameUpdate();
    }
}

// Initialize chat when page loads
document.addEventListener('DOMContentLoaded', () => {
    // Make the chat client globally accessible for debugging
    window.chatClient = new ChatClient();
    
    // Add some helpful console messages
    console.log('QB WebSocket Chat Client loaded');
    console.log('Framework: QB Actor Framework with qbm-ws module');
    console.log('WebSocket endpoint: ws://localhost:8080/ws');
    console.log('');
    console.log('Available commands for testing:');
    console.log('  chatClient.sendTestMessage("Hello from console!")');
    console.log('  chatClient.changeUsername("NewName")');
});

// Add some demo functionality
window.addEventListener('load', () => {
    // Add initial helpful message after a short delay
    setTimeout(() => {
        if (window.chatClient && !window.chatClient.connected) {
            window.chatClient.addSystemMessage(
                'WebSocket connection not available. The server may not have WebSocket support implemented yet.'
            );
            window.chatClient.addSystemMessage(
                'Check the server console for implementation status.'
            );
        }
    }, 2000);
});

// Handle page visibility changes (pause/resume connection attempts)
document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
        console.log('Page hidden - WebSocket may be paused');
    } else {
        console.log('Page visible - WebSocket resuming');
        if (window.chatClient && !window.chatClient.connected) {
            // Try to reconnect when page becomes visible again
            setTimeout(() => {
                window.chatClient.connect();
            }, 1000);
        }
    }
}); 