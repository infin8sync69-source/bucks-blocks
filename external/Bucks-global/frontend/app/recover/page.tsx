"use client";

import React, { useState } from 'react';
import { useRouter } from 'next/navigation';
import { FaShieldHalved, FaArrowLeft, FaSpinner, FaCircleCheck, FaUserGroup, FaIdCard } from 'react-icons/fa6';
import Link from 'next/link';
import { useToast } from '@/components/Toast';
import api from '@/lib/api';

export default function RecoverPage() {
    const { showToast } = useToast();
    const router = useRouter();
    const [step, setStep] = useState(1);
    const [oldPeerId, setOldPeerId] = useState('');
    const [newPeerId, setNewPeerId] = useState('');
    const [requestId, setRequestId] = useState('');
    const [isLoading, setIsLoading] = useState(false);
    const [approvals, setApprovals] = useState(0);

    const handleStartRecovery = async () => {
        if (!oldPeerId.trim() || !newPeerId.trim()) return;

        setIsLoading(true);
        try {
            const formData = new FormData();
            formData.append('old_peer_id', oldPeerId);
            formData.append('new_peer_id', newPeerId);

            const response = await api.post('/recovery/request', formData);
            const data = response.data;
            setRequestId(data.request_id);
            setStep(2);
            showToast('Recovery request initiated!', 'success');
        } catch (error: any) {
            console.error('Recovery error:', error);
            showToast('Network error during recovery', 'error');
        } finally {
            setIsLoading(false);
        }
    };

    const checkStatus = async () => {
        setIsLoading(true);
        try {
            // Simulator for guardian approvals
            // In a real app, this would poll the backend which waits for PubSub approvals
            const response = await api.post('/recovery/approve', new URLSearchParams({
                'request_id': requestId,
                'guardian_peer_id': `mock_guardian_${approvals + 1}`
            }));

            const data = response.data;
            setApprovals(data.approvals_count);
            if (data.status === 'completed') {
                setStep(3);
            }
        } catch (error) {
            console.error('Status check error:', error);
        } finally {
            setIsLoading(false);
        }
    };

    return (
        <div className="min-h-screen bg-gray-50 flex flex-col items-center justify-center p-4">
            <div className="max-w-md w-full bg-white rounded-[2.5rem] shadow-xl shadow-gray-200/50 overflow-hidden border border-gray-100">
                <div className="p-8">
                    {/* Header */}
                    <div className="flex flex-col items-center text-center mb-8">
                        <div className="w-16 h-16 bg-purple-50 text-primary rounded-2xl flex items-center justify-center text-3xl mb-4 shadow-inner">
                            <FaShieldHalved className={step === 2 ? 'animate-pulse' : ''} />
                        </div>
                        <h1 className="text-2xl font-bold text-foreground">Social Recovery</h1>
                        <p className="text-secondary text-sm mt-2">Majority-based P2P identity recovery</p>
                    </div>

                    {step === 1 && (
                        <div className="space-y-6 animate-in fade-in slide-in-from-bottom-4 duration-500">
                            <div className="space-y-4">
                                <div className="space-y-2">
                                    <label className="text-xs font-bold text-gray-400 uppercase tracking-widest pl-1">Lost Peer ID</label>
                                    <div className="relative">
                                        <FaIdCard className="absolute left-4 top-1/2 -translate-y-1/2 text-gray-400" />
                                        <input
                                            type="text"
                                            value={oldPeerId}
                                            onChange={(e) => setOldPeerId(e.target.value)}
                                            placeholder="The ID you lost access to"
                                            className="w-full pl-12 pr-4 py-4 bg-gray-50 border-none rounded-2xl text-sm focus:ring-2 focus:ring-primary/20 outline-none"
                                        />
                                    </div>
                                </div>
                                <div className="space-y-2">
                                    <label className="text-xs font-bold text-gray-400 uppercase tracking-widest pl-1">New Identity Key (DID)</label>
                                    <div className="relative">
                                        <FaIdCard className="absolute left-4 top-1/2 -translate-y-1/2 text-gray-400" />
                                        <input
                                            type="text"
                                            value={newPeerId}
                                            onChange={(e) => setNewPeerId(e.target.value)}
                                            placeholder="Your current temporary Peer ID"
                                            className="w-full pl-12 pr-4 py-4 bg-gray-50 border-none rounded-2xl text-sm focus:ring-2 focus:ring-primary/20 outline-none"
                                        />
                                    </div>
                                </div>
                            </div>

                            <button
                                onClick={handleStartRecovery}
                                disabled={isLoading || !oldPeerId || !newPeerId}
                                className="w-full bg-primary text-white py-4 rounded-2xl font-bold shadow-lg shadow-primary/20 hover:bg-primary/90 transition-all disabled:opacity-50"
                            >
                                {isLoading ? <FaSpinner className="animate-spin mx-auto" /> : "Initiate Recovery Request"}
                            </button>

                            <Link href="/login" className="block text-center text-sm font-medium text-secondary hover:text-primary transition-colors">
                                Cancel and return to Login
                            </Link>
                        </div>
                    )}

                    {step === 2 && (
                        <div className="space-y-8 animate-in zoom-in-95 duration-500">
                            <div className="text-center space-y-2">
                                <p className="text-xs font-bold text-primary uppercase tracking-widest">Awaiting Approvals</p>
                                <h3 className="text-lg font-bold">Guardian Verification</h3>
                                <p className="text-sm text-secondary">A recovery link has been broadcast to your guardians. 4 approvals required.</p>
                            </div>

                            {/* Progress Ring / Counter */}
                            <div className="flex justify-center flex-wrap gap-3">
                                {[1, 2, 3, 4, 5, 6, 7].map(i => (
                                    <div
                                        key={i}
                                        className={`w-10 h-10 rounded-full flex items-center justify-center font-bold text-sm transition-all duration-300 ${i <= approvals ? 'bg-primary text-white scale-110 shadow-lg' : 'bg-gray-100 text-gray-300 border-2 border-dashed border-gray-200'}`}
                                    >
                                        {i <= approvals ? <FaCircleCheck /> : i}
                                    </div>
                                ))}
                            </div>

                            <div className="bg-purple-50 p-4 rounded-2xl border border-purple-100 flex items-center justify-between">
                                <div className="text-left">
                                    <p className="text-[10px] font-bold text-purple-600 uppercase">Approvals</p>
                                    <p className="text-xl font-bold text-purple-900">{approvals} / 4</p>
                                </div>
                                <button
                                    onClick={checkStatus}
                                    disabled={isLoading || approvals >= 4}
                                    className="px-4 py-2 bg-white text-primary rounded-xl text-xs font-bold shadow-sm hover:shadow-md transition-all active:scale-95"
                                >
                                    {isLoading ? <FaSpinner className="animate-spin" /> : "Simulate Approval"}
                                </button>
                            </div>

                            <div className="text-center">
                                <p className="text-[10px] text-gray-400 uppercase tracking-widest mb-2">Request ID</p>
                                <code className="bg-gray-50 px-3 py-1.5 rounded-lg text-xs font-mono text-gray-600">{requestId}</code>
                            </div>
                        </div>
                    )}

                    {step === 3 && (
                        <div className="space-y-8 text-center animate-in zoom-in-95 duration-500">
                            <div className="w-20 h-20 bg-green-50 text-green-500 rounded-full flex items-center justify-center text-5xl mx-auto mb-4 border-4 border-white shadow-xl">
                                <FaCircleCheck />
                            </div>
                            <div>
                                <h2 className="text-2xl font-bold">Identity Recovered!</h2>
                                <p className="text-sm text-secondary mt-2">Your social graph and content ownership have been successfully linked to your new Identity Key.</p>
                            </div>

                            <button
                                onClick={() => {
                                    localStorage.setItem('bucks_peer_id', `did:ipfs:${newPeerId}`);
                                    localStorage.setItem('isAuthenticated', 'true');
                                    router.push('/');
                                }}
                                className="w-full bg-green-500 text-white py-4 rounded-2xl font-bold shadow-lg shadow-green-200 hover:bg-green-600 transition-all font-bold"
                            >
                                Enter Application
                            </button>
                        </div>
                    )}
                </div>
            </div>

            <p className="mt-8 text-xs text-gray-400 flex items-center gap-2">
                <FaUserGroup /> Decentralized Multi-Signature Recovery v1.0
            </p>
        </div>
    );
}
