/**
 * Bucks Wallet Logic (wallet.js)
 * Handles wallet creation, restoration, and management via the Bucks API.
 */

const API_BASE = '/api';
const REFRESH_INTERVAL = 5000;

// State
let state = {
  address: '',
  balance: 0,
  goldRate: 0,
  mnemonic: '',
  miningActive: false,
  blocksFound: 0,
  hashRate: 0,
  currentScreen: 'welcome'
};

// --- Initialization ---

document.addEventListener('DOMContentLoaded', async () => {
  initUI();
  await checkInitialState();
  startPolling();
});

function initUI() {
  // Reveal mnemonic toggle
  const mnemonicBox = document.getElementById('mnemonicBox');
  if (mnemonicBox) {
    mnemonicBox.onclick = () => {
      mnemonicBox.classList.toggle('revealed');
      const hint = document.getElementById('mnemonicHint');
      if (hint) {
        hint.textContent = mnemonicBox.classList.contains('revealed') ? '🔒 tap to hide' : '👆 tap to reveal recovery phrase';
      }
    };
  }
}

async function checkInitialState() {
  try {
    const res = await fetch(`${API_BASE}/wallets`);
    const data = await res.json();

    if (data.wallets && data.wallets.length > 0) {
      // Wallet exists, go to home
      updateStateFromWallet(data.wallets[0]);
      showScreen('home');
    } else {
      showScreen('welcome');
    }
  } catch (e) {
    console.error("Failed to check initial state:", e);
    showScreen('welcome');
  }
}

function startPolling() {
  refreshData();
  refreshMining();
  setInterval(refreshData, REFRESH_INTERVAL);
  setInterval(refreshMining, REFRESH_INTERVAL);
}

// --- Data Refreshing ---

async function refreshData() {
  try {
    // Refresh Gold Rate
    const ecoRes = await fetch(`${API_BASE}/economy/status`);
    const ecoData = await ecoRes.json();
    if (ecoData.success) {
      state.goldRate = ecoData.price_per_buck || 0;
      updateBalanceUI();
    }

    // Refresh Wallet Balance
    const walletRes = await fetch(`${API_BASE}/wallets`);
    const walletData = await walletRes.json();
    if (walletData.wallets && walletData.wallets.length > 0) {
      updateStateFromWallet(walletData.wallets[0]);
    }
  } catch (e) {
    console.warn("Refresh data error:", e);
  }
}

async function refreshMining() {
  try {
    const res = await fetch(`${API_BASE}/mining/status`);
    const data = await res.json();

    state.miningActive = data.mining;
    state.blocksFound = data.blocksFound || 0;
    state.hashRate = data.hashRate || 0;

    updateMiningUI(data);

    // Also fetch mnemonic specifically if we are on the mining tab or home
    if (state.currentScreen === 'home') {
      const pRes = await fetch(`${API_BASE}/wallets/primary`);
      if (pRes.ok) {
        const pData = await pRes.json();
        if (pData.success && pData.mnemonic) {
          state.mnemonic = pData.mnemonic;
          const mBox = document.getElementById('mnemonicBox');
          if (mBox) mBox.textContent = pData.mnemonic;
        }
      }
    }
  } catch (e) {
    console.warn("Refresh mining error:", e);
  }
}

// --- UI Updates ---

function showScreen(id) {
  state.currentScreen = id;
  document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
  const screen = document.getElementById(id);
  if (screen) screen.classList.add('active');
}

function updateStateFromWallet(wallet) {
  state.address = wallet.address;
  state.balance = wallet.balance / 100000000.0;

  document.querySelectorAll('.address').forEach(el => el.textContent = wallet.address);
  updateBalanceUI();
}

function updateBalanceUI() {
  const balEl = document.getElementById('balance');
  const goldEl = document.getElementById('gold');

  if (balEl) balEl.textContent = state.balance.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 });

  if (goldEl) {
    const totalGold = state.balance * state.goldRate;
    goldEl.textContent = `≈ ${totalGold >= 1 ? totalGold.toFixed(3) + ' g' : (totalGold * 1000).toFixed(3) + ' mg'} gold`;
  }

  const rateEl = document.getElementById('rate');
  if (rateEl) {
    if (state.goldRate === 0) {
      rateEl.textContent = "Loading rate...";
    } else {
      rateEl.textContent = `1 BUCK ≈ ${(state.goldRate >= 1 ? state.goldRate : state.goldRate * 1000).toFixed(3)} ${state.goldRate >= 1 ? 'g' : 'mg'} gold`;
    }
  }
}

function updateMiningUI(data) {
  const badge = document.getElementById('miningBadge');
  const label = document.getElementById('miningLabel');
  const btn = document.getElementById('mineBtn');

  if (badge && label && btn) {
    if (state.miningActive) {
      badge.className = 'mining-status-badge active';
      label.textContent = 'Mining';
      btn.textContent = '■ Stop Mining';
      btn.className = 'mine-stop';
    } else {
      badge.className = 'mining-status-badge stopped';
      label.textContent = 'Stopped';
      btn.textContent = '▶ Start Mining';
      btn.className = 'mine-start';
    }
  }

  const hrEl = document.getElementById('hashRate');
  if (hrEl) {
    const hr = state.hashRate;
    hrEl.textContent = hr > 1000 ? `${(hr / 1000).toFixed(1)} KH/s` : `${hr} H/s`;
  }

  const bfEl = document.getElementById('blocksFound');
  if (bfEl) bfEl.textContent = state.blocksFound;

  const maEl = document.getElementById('minerAddr');
  if (maEl) maEl.textContent = data.minerAddress || state.address || '—';
}

// --- Wallet Actions ---

async function createNewWallet() {
  // Step 1: Generate Mnemonic
  try {
    const res = await fetch(`${API_BASE}/wallets/mnemonic/generate`);
    const data = await res.json();
    if (data.success) {
      state.tempMnemonic = data.mnemonic;
      document.getElementById('displayMnemonic').textContent = data.mnemonic;
      showScreen('recovery');
    } else {
      alert("Failed to generate mnemonic: " + data.error);
    }
  } catch (e) {
    alert("Server error during generation");
  }
}

async function finalizeCreation() {
  const saved = document.getElementById('savedCheck').checked;
  if (!saved) {
    alert("Please confirm you have saved your recovery phrase.");
    return;
  }

  try {
    const res = await fetch(`${API_BASE}/wallets/restore`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mnemonic: state.tempMnemonic, passphrase: '' })
    });
    const data = await res.json();

    if (data.success) {
      updateStateFromWallet(data.wallet);
      showScreen('home');
      alert("Wallet successfully created and encrypted!");
    } else {
      alert("Error finalizing wallet: " + data.error);
    }
  } catch (e) {
    alert("Network error during finalization");
  }
}

async function restoreWallet() {
  const mnemonic = document.getElementById('restoreMnemonic').value.trim();
  if (!mnemonic) {
    alert("Please enter your recovery phrase.");
    return;
  }

  try {
    const res = await fetch(`${API_BASE}/wallets/restore`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mnemonic: mnemonic, passphrase: '' })
    });
    const data = await res.json();

    if (data.success) {
      updateStateFromWallet(data.wallet);
      showScreen('home');
    } else {
      alert("Restoration failed: " + data.error);
    }
  } catch (e) {
    alert("Network error during restoration");
  }
}

async function toggleMining() {
  const endpoint = state.miningActive ? 'stop' : 'start';
  try {
    await fetch(`${API_BASE}/mining/${endpoint}`, { method: 'POST' });
    setTimeout(refreshMining, 500);
  } catch (e) {
    console.error("Toggle mining error:", e);
  }
}

async function sendTransaction() {
  const to = document.getElementById('recipientAddr').value.trim();
  const amount = parseFloat(document.getElementById('sendAmount').value);

  if (!to || isNaN(amount) || amount <= 0) {
    alert("Invalid recipient or amount");
    return;
  }

  try {
    const res = await fetch(`${API_BASE}/transactions/send`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        from: state.address,
        to: to,
        amount: Math.floor(amount * 100000000)
      })
    });
    const data = await res.json();
    if (data.success) {
      alert("Transaction broadcasted successfully!");
      document.getElementById('sendAmount').value = '';
      document.getElementById('recipientAddr').value = '';
      refreshData();
    } else {
      alert("Transaction failed: " + data.error);
    }
  } catch (e) {
    alert("Network error during transaction");
  }
}

// --- Navigation Helpers ---

function switchTab(id, el) {
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  el.classList.add('active');
  document.querySelectorAll('.tabview').forEach(v => v.style.display = 'none');
  document.getElementById(id).style.display = 'block';
}

function updateGoldCalc() {
  const amount = parseFloat(document.getElementById('sendAmount').value) || 0;
  const goldEquivalent = amount * state.goldRate;
  document.getElementById('sendGold').value = goldEquivalent >= 1
    ? `${goldEquivalent.toFixed(3)} g`
    : `${(goldEquivalent * 1000).toFixed(3)} mg`;
}

// Export functions for HTML onclick attributes
window.go = showScreen;
window.switchTab = switchTab;
window.createNewWallet = createNewWallet;
window.finalizeCreation = finalizeCreation;
window.restoreWallet = restoreWallet;
window.toggleMining = toggleMining;
window.sendTransaction = sendTransaction;
window.updateGoldCalc = updateGoldCalc;
