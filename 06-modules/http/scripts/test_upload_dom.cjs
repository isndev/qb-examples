// Exercise the shipped browser script against a small DOM fixture without a browser package.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class Element {
    constructor(tagName) {
        this.tagName = tagName;
        this.children = [];
        this.dataset = {};
        this.listeners = {};
        this.className = '';
        this._text = '';
        this.classList = {
            contains: name => this.className.split(/\s+/).includes(name),
            add: () => {},
            remove: () => {}
        };
    }

    set textContent(value) {
        this._text = String(value);
        this.children = [];
    }

    get textContent() {
        return this._text + this.children.map(child => child.textContent).join('');
    }

    set innerHTML(value) {
        // An HTML string cannot be treated as safe text in this fixture.
        this.rawHTML = value;
        this.children = [];
    }

    appendChild(child) {
        this.children.push(child);
        return child;
    }

    replaceChildren(...children) {
        this._text = '';
        this.children = children;
    }

    addEventListener(type, listener) {
        this.listeners[type] = listener;
    }

    removeEventListener(type) {
        delete this.listeners[type];
    }

    getAttribute(name) {
        return name === 'data-filename' ? this.dataset.filename : undefined;
    }
}

function descendants(root, tagName) {
    return root.children.flatMap(child => [
        ...(child.tagName === tagName ? [child] : []),
        ...descendants(child, tagName)
    ]);
}

async function main() {
    const filename = 'report # & " <>.txt';
    const description = 'description <draft> & "quoted"';
    const tags = ['alpha & beta', '<tag>', '"quote"'];
    const mimeType = 'text/<plain> & "quoted"';
    const elements = Object.fromEntries(
        ['uploadForm', 'fileInput', 'descriptionInput', 'tagsInput', 'uploadProgress',
            'uploadStatus', 'clearBtn', 'refreshBtn', 'filesList'].map(id => [id, new Element(id)])
    );
    const calls = [];
    const document = {
        getElementById: id => elements[id],
        createElement: tag => new Element(tag),
        createTextNode: value => {
            const node = new Element('#text');
            node.textContent = value;
            return node;
        },
        addEventListener: () => {}
    };
    const context = {
        document,
        console,
        confirm: () => true,
        QBHttpUtils: {formatFileSize: size => `${size} B`},
        fetch: async (target, options = {}) => {
            calls.push({target, method: options.method || 'GET'});
            if (target === '/api/files') {
                return {json: async () => ({files: [{
                    filename, size: 17,
                    metadata: {description, tags, mime_type: mimeType, last_modified: 1}
                }]})};
            }
            if (options.method === 'DELETE') {
                return {json: async () => ({success: true})};
            }
            throw new Error(`unexpected request: ${target}`);
        }
    };
    const script = process.argv[2] || path.join(__dirname, '..', 'resources', 'static', 'upload.js');
    vm.runInNewContext(fs.readFileSync(script, 'utf8'), context, {filename: script});
    context.loadFilesList();
    await new Promise(resolve => setImmediate(resolve));

    const list = elements.filesList;
    assert.equal(list.rawHTML, undefined, 'file data is never parsed as HTML');
    assert.equal(descendants(list, 'tr').length, 2, 'one data row remains intact');
    assert.equal(descendants(list, 'strong')[0]?.textContent, filename, 'filename is visible as text');
    assert.deepEqual(descendants(list, 'small').map(item => item.textContent),
        [description, `Tags: ${tags.join(', ')}`], 'description and tags are visible as text');
    assert.equal(descendants(list, 'td')[2]?.textContent, mimeType, 'MIME is visible as text');
    assert.equal(descendants(list, 'img').length, 0, 'metadata created no markup nodes');

    const download = descendants(list, 'a')[0];
    const url = new URL(download?.href, 'http://localhost:8080');
    assert.equal(url.hash, '', 'filename hash is not a URL fragment');
    assert.equal(decodeURIComponent(url.pathname.split('/').pop()), filename, 'download link keeps the exact name');
    const remove = descendants(list, 'button')[0];
    assert.equal(remove?.dataset.filename, filename, 'delete button keeps the exact name');
    list.listeners.click({target: remove});
    await new Promise(resolve => setImmediate(resolve));
    assert.ok(calls.some(call => call.method === 'DELETE'
        && call.target === '/api/files/' + encodeURIComponent(filename)), 'delete encodes one path segment');
    console.log('PASS DOM labels and download/delete links preserve special characters');
}

main().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
