"use client";

import React, { useState } from 'react';
import { useRouter } from 'next/navigation';
import { FaQrcode, FaUserPlus, FaArrowLeft, FaCamera, FaSpinner, FaCircleCheck } from 'react-icons/fa6';
import Link from 'next/link';
import { useToast } from '@/components/Toast';
import api from '@/lib/api';

export default function QRScanPage() {
    const { showToast } = useToast();
    const router = useRouter();
    const [did, setDid] = useState('');
    const [isConnecting, setIsConnecting] = useState(false);
    const [success, setSuccess] = useState(false);
    const [scannedPeer, setScannedPeer] = useState<any>(null);

    const handleConnect = async (inputDid?: string) => {
        const targetDid = inputDid || did;
        if (!targetDid.trim()) return;

        setIsConnecting(true);
        try {
            // Send the DID directly to the backend to resolve and add
            const formData = new FormData();
            formData.append('did', targetDid);

            const response = await api.post('/contacts/add', formData);
            const data = response.data;
            setScannedPeer(data);
            setSuccess(true);
            // Redirect after a short delay
            setTimeout(() => {
                router.push('/messages');
            }, 2000);
        } catch (error: any) {
            console.error('Connect error:', error);
            showToast(error.response?.data?.detail || 'Failed to add contact', 'error');
        } finally {
            setIsConnecting(false);
        }
    };

    const simulateScan = () => {
        // Mock a scanned DID (using a common public IPFS peer or a test one)
        const mockDid = "did:ipfs:12D3KooWHz4NfUv3Y4y5Hq7R6T1V2W3X4Y5Z6A7B8C9D0E";
        setDid(mockDid);
        handleConnect(mockDid);
    };

    return (
        <div className="min-h-screen bg-white">
            <div className="p-4 flex items-center gap-4 border-b border-gray-100">
                <Link href="/syncing" className="p-2 hover:bg-gray-100 rounded-full transition-colors">
                    <FaArrowLeft className="text-secondary" />
                </Link>
                <h1 className="text-xl font-bold">Connect Contact</h1>
            </div>

            <div className="max-w-md mx-auto p-6 flex flex-col items-center">
                {/* Scanner View Simulation */}
                <div className="w-full aspect-square bg-black rounded-3xl mb-8 relative overflow-hidden shadow-2xl">
                    {!success ? (
                        <>
                            <div className="absolute inset-0 flex items-center justify-center">
                                <FaCamera className="text-white/20 text-6xl animate-pulse" />
                            </div>
                            <div className="absolute inset-8 border-2 border-primary/50 rounded-2xl border-dashed"></div>
                            <div className="absolute top-0 left-0 right-0 h-1 bg-primary/50 animate-scanner"></div>
                            <div className="absolute bottom-6 left-0 right-0 flex justify-center">
                                <button
                                    onClick={simulateScan}
                                    className="bg-white/20 hover:bg-white/30 backdrop-blur-md text-white px-6 py-2 rounded-full text-sm font-bold transition-all border border-white/30"
                                >
                                    Simulate QR Scan
                                </button>
                            </div>
                        </>
                    ) : (
                        <div className="absolute inset-0 bg-primary flex flex-col items-center justify-center text-white animate-in zoom-in-95 duration-300">
                            <FaCircleCheck className="text-6xl mb-4" />
                            <h2 className="text-2xl font-bold">Connected!</h2>
                            <p className="text-white/80 mt-2">Added as trusted contact</p>
                        </div>
                    )}
                </div>

                {!success && (
                    <div className="w-full space-y-6">
                        <div className="text-center">
                            <p className="text-secondary text-sm">Scan a friend's DID QR code or enter it manually below to establish a direct P2P connection.</p>
                        </div>

                        <div className="space-y-3">
                            <label className="text-xs font-bold text-gray-400 uppercase tracking-widest pl-1">Manual Entry</label>
                            <div className="flex gap-2">
                                <input
                                    type="text"
                                    value={did}
                                    onChange={(e) => setDid(e.target.value)}
                                    placeholder="did:ipfs:..."
                                    className="flex-1 px-4 py-3 bg-gray-50 border-none rounded-2xl text-sm focus:ring-2 focus:ring-primary/20 outline-none"
                                />
                                <button
                                    onClick={() => handleConnect()}
                                    disabled={isConnecting || !did.trim()}
                                    className="bg-primary text-white p-4 rounded-2xl shadow-lg shadow-primary/20 hover:bg-primary/90 disabled:opacity-50 transition-all"
                                >
                                    {isConnecting ? <FaSpinner className="animate-spin" /> : <FaUserPlus />}
                                </button>
                            </div>
                        </div>

                        <div className="pt-4 flex flex-col items-center opacity-40">
                            <FaQrcode className="text-6xl mb-2" />
                            <span className="text-xs font-medium">Your Public DID QR</span>
                        </div>
                    </div>
                )}
            </div>

            <style jsx>{`
                @keyframes scanner {
                    0% { top: 0; }
                    50% { top: 100%; }
                    100% { top: 0; }
                }
                .animate-scanner {
                    position: absolute;
                    width: 100%;
                    animation: scanner 3s ease-in-out infinite;
                }
            `}</style>
        </div>
    );
}
