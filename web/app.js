const API_BASE = 'http://localhost:8080/api';

let wallets = [];
let currentBlockchain = null;
let miningActive = false;
let hashRateInterval = null;

// Initialize app
document.addEventListener('DOMContentLoaded', () => {
    setupTabs();
    setupEventListeners();
    checkConnection();
    loadDashboard();
    loadWallets();
    
    // Polling
    setInterval(checkConnection, 5000);
    setInterval(loadDashboard, 10000);
});

function setupTabs() {
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabContents = document.querySelectorAll('.tab-content');

    tabButtons.forEach(btn => {
        btn.addEventListener('click', () => {
            const targetTab = btn.getAttribute('data-tab');

            tabButtons.forEach(b => b.classList.remove('active'));
            tabContents.forEach(c => c.classList.remove('active'));

            btn.classList.add('active');
            const targetEl = document.getElementById(targetTab);
            if (targetEl) targetEl.classList.add('active');

            // Load tab-specific data
            refreshTabData(targetTab);
        });
    });
}

function refreshTabData(tab) {
    if (tab === 'dashboard') loadDashboard();
    else if (tab === 'transactions') { loadWallets(); loadTransactions(); }
    else if (tab === 'blocks') { /* Search based */ }
    else if (tab === 'mining') loadMining();
}

function setupEventListeners() {
    document.getElementById('transactionForm')?.addEventListener('submit', handleTransactionSubmit);
    document.getElementById('mineBlockBtn')?.addEventListener('click', mineBlock);
    document.getElementById('searchBlockBtn')?.addEventListener('click', searchBlock);
}

// --- Connection & Status ---
async function checkConnection() {
    try {
        const response = await fetch(`${API_BASE}/status`);
        if (response.ok) updateStatus(true);
        else updateStatus(false);
    } catch (error) {
        updateStatus(false);
    }
}

function updateStatus(connected) {
    const indicator = document.getElementById('statusIndicator');
    const statusText = document.getElementById('statusText');
    const dot = indicator?.querySelector('.status-dot');

    if (indicator && statusText && dot) {
        if (connected) {
            dot.className = 'status-dot connected';
            statusText.textContent = 'Node Online';
        } else {
            dot.className = 'status-dot disconnected';
            statusText.textContent = 'Node Offline';
        }
    }
}

// --- Wallet Wizard Logic ---
function openWizard() {
    document.getElementById('setupWizard').style.display = 'flex';
    showStep(1);
}

function closeWizard() {
    document.getElementById('setupWizard').style.display = 'none';
}

function showStep(step) {
    document.querySelectorAll('.wizard-step').forEach(s => s.classList.remove('active'));
    if (step === 1) document.getElementById('wizardStep1').classList.add('active');
    else if (step === 2) document.getElementById('wizardStep2').classList.add('active');
    else if (step === 'restore') document.getElementById('wizardRestoreStep').classList.add('active');
}

function showRestoreStep() { showStep('restore'); }

async function generateNewWallet() {
    try {
        const response = await fetch(`${API_BASE}/wallets/create`, { method: 'POST' });
        const data = await response.json();

        if (data.success) {
            const mnemonic = data.wallet.mnemonic;
            displayMnemonic(mnemonic);
            showStep(2);
            loadWallets();
        } else {
            alert('Failed to generate wallet: ' + data.error);
        }
    } catch (error) {
        alert('Error: ' + error.message);
    }
}

function displayMnemonic(mnemonic) {
    const container = document.getElementById('mnemonicDisplay');
    const words = mnemonic.split(' ');
    container.innerHTML = words.map((w, i) => `
        <div class="mnemonic-word"><span>${i + 1}.</span> ${w}</div>
    `).join('');
}

async function submitRestore() {
    const mnemonic = document.getElementById('restoreMnemonic').value.trim();
    if (!mnemonic) return alert('Please enter your mnemonic');

    try {
        const response = await fetch(`${API_BASE}/wallets/restore`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mnemonic })
        });
        const data = await response.json();

        if (data.success) {
            alert('Wallet restored successfully!');
            finishWalletSetup();
        } else {
            alert('Restoration failed: ' + data.error);
        }
    } catch (error) {
        alert('Error: ' + error.message);
    }
}

function finishWalletSetup() {
    closeWizard();
    loadWallets();
    document.getElementById('walletSetupBtn').innerHTML = '<i class="fas fa-check-circle"></i> Wallet Ready';
}

// --- Dashboard & Explorer ---
async function loadDashboard() {
    try {
        const response = await fetch(`${API_BASE}/blockchain/info`);
        const data = await response.json();
        currentBlockchain = data;

        document.getElementById('chainHeight').textContent = data.height || 0;
        document.getElementById('difficulty').textContent = data.difficulty || 0;
        document.getElementById('utxoCount').textContent = data.utxoCount || 0;
        
        // Recent blocks
        const blocksRes = await fetch(`${API_BASE}/blocks/recent`);
        const blocksData = await blocksRes.json();
        displayRecentBlocks(blocksData.blocks || []);
    } catch (e) {
        console.error('Dash error:', e);
    }
}

function displayRecentBlocks(blocks) {
    const container = document.getElementById('recentBlocks');
    if (!container) return;

    if (blocks.length === 0) {
        container.innerHTML = '<div class="empty-state">No blocks found</div>';
        return;
    }

    container.innerHTML = blocks.reverse().map(block => `
        <div class="block-item">
            <div class="block-header">
                <h3>Block #${block.height}</h3>
                <span class="block-hash">${block.hash.substring(0, 24)}...</span>
            </div>
            <div class="block-info">
                <div class="block-info-item"><strong>TXs:</strong> ${block.transactionCount || 0}</div>
                <div class="block-info-item"><strong>Nonce:</strong> ${block.nonce || 0}</div>
                <div class="block-info-item"><strong>Time:</strong> ${new Date(block.timestamp * 1000).toLocaleTimeString()}</div>
            </div>
        </div>
    `).join('');
}

// --- Wallets & Transactions ---
async function loadWallets() {
    try {
        const response = await fetch(`${API_BASE}/wallets`);
        const data = await response.json();
        wallets = data.wallets || [];
        displayWallets(wallets);
        updateWalletSelects();
    } catch (e) { console.error('Wallet error:', e); }
}

function displayWallets(walletList) {
    const container = document.getElementById('walletsList');
    if (!container) return;

    if (walletList.length === 0) {
        container.innerHTML = '<div class="empty-state">No wallets found. Use "Setup Wallet" to begin.</div>';
        return;
    }

    container.innerHTML = walletList.map(w => `
        <div class="wallet-item">
            <div class="wallet-details">
                <div class="wallet-address">${w.address}</div>
                <div class="wallet-balance">${(w.balance / 100000000).toFixed(8)} BUCKS</div>
            </div>
            <button class="btn btn-secondary btn-small" onclick="copyText('${w.address}')">Copy</button>
        </div>
    `).join('');
}

function updateWalletSelects() {
    const fromSelect = document.getElementById('fromWallet');
    const minerSelect = document.getElementById('minerWallet');
    if (!fromSelect || !minerSelect) return;

    const options = wallets.map((w, i) => `
        <option value="${i}">${w.address.substring(0, 15)}... (${(w.balance / 100000000).toFixed(2)})</option>
    `).join('');

    fromSelect.innerHTML = options || '<option value="">No Wallets</option>';
    minerSelect.innerHTML = options || '<option value="">No Wallets</option>';
}

async function handleTransactionSubmit(e) {
    e.preventDefault();
    const fromIdx = document.getElementById('fromWallet').value;
    const to = document.getElementById('toAddress').value;
    const amount = document.getElementById('amount').value;

    if (!to || !amount) return alert('Missing fields');

    try {
        const res = await fetch(`${API_BASE}/transactions/send`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                from: wallets[fromIdx].address,
                to,
                amount: Math.floor(amount * 100000000)
            })
        });
        const data = await res.json();
        if (data.success) {
            alert('Transaction Sent!');
            document.getElementById('transactionForm').reset();
            loadTransactions();
            loadWallets();
        } else alert('Error: ' + data.error);
    } catch (e) { alert('Error: ' + e.message); }
}

async function loadTransactions() {
    try {
        const res = await fetch(`${API_BASE}/transactions`);
        const data = await res.json();
        const container = document.getElementById('transactionHistory');
        if (!container) return;

        const txs = data.transactions || [];
        if (txs.length === 0) {
            container.innerHTML = '<div class="empty-state">No transactions</div>';
            return;
        }

        container.innerHTML = txs.map(tx => `
            <div class="transaction-item">
                <div class="transaction-hash">${tx.hash.substring(0, 40)}...</div>
                <div class="transaction-amount">${(tx.totalAmount / 100000000).toFixed(8)} BUCKS</div>
            </div>
        `).join('');
    } catch (e) { console.error('TX history error:', e); }
}

// --- Mining Logic ---
async function loadMining() {
    if (currentBlockchain) {
        document.getElementById('currentDifficulty').textContent = currentBlockchain.difficulty;
    }
}

async function mineBlock() {
    const minerIdx = document.getElementById('minerWallet').value;
    if (minerIdx === "" || wallets.length === 0) return alert('Select a miner wallet');

    startMiningAnimation();

    try {
        const res = await fetch(`${API_BASE}/mining/mine`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ minerWalletIndex: parseInt(minerIdx) })
        });
        const data = await res.json();
        
        stopMiningAnimation();

        if (data.success) {
            const minedCount = document.getElementById('blocksMined');
            minedCount.textContent = (parseInt(minedCount.textContent) + 1);
            showMiningSuccess(data.block.nonce);
            loadDashboard();
            loadWallets();
        } else {
            alert('Mining failed: ' + data.error);
        }
    } catch (e) {
        stopMiningAnimation();
        alert('Mining Error: ' + e.message);
    }
}

function startMiningAnimation() {
    miningActive = true;
    const display = document.getElementById('miningStatus');
    const text = display.querySelector('.status-text');
    display.classList.add('active');
    text.textContent = 'Mining in progress...';
    
    let hashes = 0;
    hashRateInterval = setInterval(() => {
        hashes += Math.floor(Math.random() * 500) + 100;
        document.getElementById('hashRate').textContent = `${(hashes / 100).toFixed(2)} KH/s`;
    }, 100);
}

function stopMiningAnimation() {
    miningActive = false;
    clearInterval(hashRateInterval);
    const display = document.getElementById('miningStatus');
    display.classList.remove('active');
}

function showMiningSuccess(nonce) {
    const text = document.querySelector('#miningStatus .status-text');
    text.innerHTML = `<span style="color: #10b981"><i class="fas fa-check"></i> Found Block! Nonce: ${nonce}</span>`;
    setTimeout(() => {
        if (!miningActive) text.textContent = 'Standby';
    }, 5000);
}

// --- Explorer ---
async function searchBlock() {
    const query = document.getElementById('blockSearch').value.trim();
    if (!query) return;

    try {
        const url = isNaN(query) ? `${API_BASE}/blocks/hash/${query}` : `${API_BASE}/blocks/height/${query}`;
        const res = await fetch(url);
        const data = await res.json();

        const container = document.getElementById('blockDetails');
        if (data.success && data.block) {
            const b = data.block;
            container.innerHTML = `
                <div class="block-details-grid">
                    <div class="detail-row"><span class="label">Hash:</span> <span class="value">${b.hash}</span></div>
                    <div class="detail-row"><span class="label">Height:</span> <span class="value">${b.height}</span></div>
                    <div class="detail-row"><span class="label">Prev Hash:</span> <span class="value">${b.prevHash}</span></div>
                    <div class="detail-row"><span class="label">Merkle:</span> <span class="value">${b.merkleRoot}</span></div>
                    <div class="detail-row"><span class="label">Time:</span> <span class="value">${new Date(b.timestamp * 1000).toLocaleString()}</span></div>
                </div>
            `;
        } else container.innerHTML = '<div class="error">Block not found</div>';
    } catch (e) { alert('Search error: ' + e.message); }
}

// --- Utilities ---
function copyText(text) {
    navigator.clipboard.writeText(text).then(() => alert('Copied to clipboard!'));
}
