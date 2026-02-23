"use client";

import React, { useState } from 'react';
import { useRouter } from 'next/navigation';
import { FaIdCard, FaFileImport, FaCircleCheck, FaDownload, FaArrowRight, FaFingerprint } from 'react-icons/fa6';
import Link from 'next/link';
import { useToast } from '@/components/Toast';

export default function LoginPage() {
    const { showToast } = useToast();
    const [step, setStep] = useState<'options' | 'generate' | 'import'>('options');
    const [newIdentity, setNewIdentity] = useState<{ did: string; secret: string } | null>(null);
    const [wallets, setWallets] = useState<any[]>([]);
    const [isSaving, setIsSaving] = useState(false);
    const router = useRouter();

    const handleGenerate = async () => {
        setIsSaving(true);
        try {
            // In a real DID system, this would happen in a Web Worker or library
            // For now, we fetch a newly generated identity from the backend
            const response = await fetch(`http://${window.location.hostname}:8000/api/auth/generate-identity`, {
                method: 'POST'
            });
            const data = await response.json();
            setNewIdentity(data);
            setStep('generate');
        } catch (error) {
            console.error('Failed to generate identity', error);
            showToast('Error generating identity. Please try again.', 'error');
        } finally {
            setIsSaving(false);
        }
    };

    const handleDownload = () => {
        if (!newIdentity) return;
        const blob = new Blob([JSON.stringify(newIdentity, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `ipfs_identity_${newIdentity.did.substring(0, 8)}.json`;
        a.click();
        URL.revokeObjectURL(url);
    };

    const handleComplete = () => {
        // Save to local storage for session management
        if (newIdentity) {
            localStorage.setItem('bucks_peer_id', newIdentity.did);
            if (newIdentity.secret) {
                localStorage.setItem('bucks_identity_secret', newIdentity.secret);
            }
            localStorage.setItem('isAuthenticated', 'true');
            // Redirect to settings for onboarding
            router.push('/settings?onboarding=true');
        }
    };

    const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = async (event) => {
            try {
                const identity = JSON.parse(event.target?.result as string);
                if (identity.did && identity.secret) {
                    localStorage.setItem('bucks_peer_id', identity.did);
                    localStorage.setItem('bucks_identity_secret', identity.secret);
                    localStorage.setItem('isAuthenticated', 'true');
                    showToast('Identity imported successfully!', 'success');
                    router.push('/feed');
                } else {
                    showToast('Invalid identity file format.', 'error');
                }
            } catch (err) {
                showToast('Failed to parse identity file.', 'error');
            }
        };
        reader.readAsText(file);
    };

    const handleBucksWalletLogin = async () => {
        setIsSaving(true);
        try {
            // 1. Fetch wallets from C++ server (Blockchain Node)
            const response = await fetch(`http://${window.location.hostname}:8080/api/wallets`);
            const data = await response.json();

            if (data.wallets && data.wallets.length > 0) {
                // For MVP, just pick the first wallet
                const wallet = data.wallets[0];

                // Save to local storage
                localStorage.setItem('bucks_identity_type', 'bucks_wallet');
                localStorage.setItem('bucks_wallet_address', wallet.address);
                localStorage.setItem('isAuthenticated', 'true');

                // The API interceptor will handle the DID conversion from the public key returned during signing
                // For now, we store a placeholder or just use the address as the peer ID until first request
                localStorage.setItem('bucks_peer_id', `bucks_wallet:${wallet.address}`);

                showToast(`Connected to Bucks Wallet: ${wallet.address.substring(0, 10)}...`, 'success');
                router.push('/feed');
            } else {
                showToast('No Bucks Wallets found. Please create one in the Bucks App first.', 'warning');
            }
        } catch (error) {
            console.error('Failed to connect to Bucks Wallet', error);
            showToast('Could not connect to Bucks Blockchain server (Port 8080).', 'error');
        } finally {
            setIsSaving(false);
        }
    };

    return (
        <div className="min-h-screen bg-gray-50 flex flex-col items-center justify-center p-6 bg-[radial-gradient(circle_at_top_right,_var(--tw-gradient-stops))] from-blue-50 via-white to-purple-50">
            <div className="w-full max-w-md bg-white rounded-3xl shadow-xl border border-gray-100 p-8">
                {/* Logo Section */}
                <div className="flex flex-col items-center mb-10 text-center">
                    <div className="w-16 h-16 bg-primary rounded-2xl flex items-center justify-center text-white text-3xl mb-4 shadow-lg shadow-primary/30">
                        <FaFingerprint />
                    </div>
                    <h1 className="text-2xl font-bold text-foreground">Decentralized Feed</h1>
                    <p className="text-secondary text-sm mt-2">Your identity, your data, your ownership.</p>
                </div>

                {step === 'options' && (
                    <div className="space-y-4">
                        <button
                            onClick={handleGenerate}
                            disabled={isSaving}
                            className="w-full p-4 rounded-2xl border-2 border-gray-100 hover:border-primary/50 hover:bg-primary/5 transition-all text-left flex items-center space-x-4 group"
                        >
                            <div className="w-12 h-12 rounded-xl bg-blue-50 flex items-center justify-center text-primary group-hover:scale-110 transition-transform">
                                <FaIdCard className="text-xl" />
                            </div>
                            <div className="flex-1">
                                <h3 className="font-bold text-foreground">New Identity</h3>
                                <p className="text-xs text-secondary">Generate and download your unique P2P DID.</p>
                            </div>
                            <FaArrowRight className="text-gray-300 group-hover:text-primary transition-colors" />
                        </button>

                        <button
                            onClick={handleBucksWalletLogin}
                            disabled={isSaving}
                            className="w-full p-4 rounded-2xl border-2 border-amber-100 bg-amber-50/30 hover:border-amber-500/50 hover:bg-amber-50 transition-all text-left flex items-center space-x-4 group shadow-sm shadow-amber-900/5"
                        >
                            <div className="w-12 h-12 rounded-xl bg-amber-500/20 flex items-center justify-center text-amber-600 group-hover:scale-110 transition-transform">
                                <FaFingerprint className="text-xl" />
                            </div>
                            <div className="flex-1">
                                <h3 className="font-bold text-amber-900">Login with Bucks Wallet</h3>
                                <p className="text-xs text-amber-700/70">Use your existing Blockchain identity (SSO).</p>
                            </div>
                            <FaArrowRight className="text-amber-500/40 group-hover:text-amber-600 transition-colors" />
                        </button>

                        <a
                            href="/ipfs_node.zip"
                            download
                            className="w-full p-4 rounded-2xl border-2 border-primary/20 bg-primary/5 hover:border-primary/50 hover:bg-primary/10 transition-all text-left flex items-center space-x-4 group"
                        >
                            <div className="w-12 h-12 rounded-xl bg-primary/20 flex items-center justify-center text-primary group-hover:scale-110 transition-transform">
                                <FaDownload className="text-xl" />
                            </div>
                            <div className="flex-1">
                                <h3 className="font-bold text-primary">Run Your Own Node</h3>
                                <p className="text-xs text-secondary">Download & host your own instance connected to our swarm.</p>
                            </div>
                            <FaArrowRight className="text-primary/40 group-hover:text-primary transition-colors" />
                        </a>

                        <button
                            onClick={() => setStep('import')}
                            className="w-full p-4 rounded-2xl border-2 border-gray-100 hover:border-purple/50 hover:bg-purple-50 transition-all text-left flex items-center space-x-4 group"
                        >
                            <div className="w-12 h-12 rounded-xl bg-purple-50 flex items-center justify-center text-purple-600 group-hover:scale-110 transition-transform">
                                <FaFileImport className="text-xl" />
                            </div>
                            <div className="flex-1">
                                <h3 className="font-bold text-foreground">Import Identity</h3>
                                <p className="text-xs text-secondary">Upload your `identity.json` file to log back in.</p>
                            </div>
                            <FaArrowRight className="text-gray-300 group-hover:text-purple transition-colors" />
                        </button>

                        <div className="pt-6 text-center">
                            <Link href="/recover" className="text-sm font-medium text-primary hover:underline">
                                Lost my identity? Try Social Recovery
                            </Link>
                        </div>
                    </div>
                )}

                {step === 'generate' && newIdentity && (
                    <div className="space-y-6">
                        <div className="bg-green-50/50 p-4 rounded-2xl border border-green-100 flex items-center space-x-3 text-green-700">
                            <FaCircleCheck className="text-xl" />
                            <p className="text-sm font-medium">Identity successfully generated!</p>
                        </div>

                        <div className="space-y-4 pt-2">
                            <div className="p-4 bg-gray-50 rounded-xl border border-gray-100">
                                <p className="text-[10px] font-bold text-gray-400 uppercase tracking-wider mb-2">Your New DID</p>
                                <p className="text-xs font-mono text-gray-700 break-all leading-relaxed">
                                    {newIdentity.did}
                                </p>
                            </div>

                            <p className="text-xs text-secondary leading-relaxed bg-amber-50 p-3 rounded-lg border border-amber-100">
                                <strong className="text-amber-800">Warning:</strong> Download this file and keep it safe. There are no passwords to reset if you lose this key.
                            </p>

                            <button
                                onClick={handleDownload}
                                className="w-full py-4 bg-white border-2 border-primary text-primary rounded-xl font-bold flex items-center justify-center space-x-2 hover:bg-primary hover:text-white transition-all shadow-sm"
                            >
                                <FaDownload />
                                <span>Download Identity File</span>
                            </button>

                            <button
                                onClick={handleComplete}
                                className="w-full py-4 bg-primary text-white rounded-xl font-bold shadow-lg shadow-primary/20 hover:bg-primary/90 transition-all"
                            >
                                Enter Application
                            </button>
                        </div>
                    </div>
                )}

                {step === 'import' && (
                    <div className="space-y-6">
                        <div className="flex items-center justify-between mb-2">
                            <button onClick={() => setStep('options')} className="text-sm text-secondary hover:text-primary transition-colors">
                                ← Back
                            </button>
                            <h2 className="font-bold text-foreground">Import Identity</h2>
                        </div>

                        <label className="flex flex-col items-center justify-center w-full h-48 border-2 border-dashed border-gray-100 rounded-3xl cursor-pointer hover:bg-gray-50 transition-all group overflow-hidden relative">
                            <div className="flex flex-col items-center justify-center pt-5 pb-6">
                                <FaFileImport className="text-3xl text-gray-300 mb-4 group-hover:scale-110 transition-transform" />
                                <p className="mb-2 text-sm text-gray-500 font-medium">Click to upload identity file</p>
                                <p className="text-xs text-gray-400">ipfs_identity_*.json</p>
                            </div>
                            <input type="file" className="hidden" accept=".json" onChange={handleFileUpload} />
                        </label>
                    </div>
                )}
            </div>

            <p className="mt-8 text-secondary text-sm">
                Next-gen Decentralized Social Graph v1.0
            </p>
        </div>
    );
}
