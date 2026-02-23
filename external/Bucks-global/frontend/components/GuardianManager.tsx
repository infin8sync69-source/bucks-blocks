"use client";

import React, { useState, useEffect } from 'react';
import { FaShieldHalved, FaUserPlus, FaTrash, FaCircleCheck, FaSpinner, FaCircleInfo } from 'react-icons/fa6';

interface GuardianManagerProps {
    onClose?: () => void;
}

export default function GuardianManager({ onClose }: GuardianManagerProps) {
    const [guardians, setGuardians] = useState<string[]>([]);
    const [contacts, setContacts] = useState<any[]>([]);
    const [loading, setLoading] = useState(true);
    const [actionLoading, setActionLoading] = useState<string | null>(null);

    const fetchData = async () => {
        try {
            const host = typeof window !== 'undefined' ? window.location.hostname : 'localhost';
            const [gRes, cRes] = await Promise.all([
                fetch(`http://${host}:8000/api/guardians`),
                fetch(`http://${host}:8000/api/following`)
            ]);

            const gData = await gRes.json();
            const cData = await cRes.json();

            setGuardians(gData.guardians || []);
            // Only show 'contact' tier as potential guardians
            setContacts(cData.following.filter((f: any) => f.relationship_type === 'contact'));
        } catch (error) {
            console.error('Failed to fetch guardian data', error);
        } finally {
            setLoading(false);
        }
    };

    useEffect(() => {
        fetchData();
    }, []);

    const addGuardian = async (peerId: string) => {
        if (guardians.length >= 7) {
            alert("Maximum 7 guardians allowed.");
            return;
        }

        setActionLoading(peerId);
        try {
            const formData = new FormData();
            formData.append('peer_id', peerId);

            const host = typeof window !== 'undefined' ? window.location.hostname : 'localhost';
            const res = await fetch(`http://${host}:8000/api/guardians/add`, {
                method: 'POST',
                body: formData
            });

            if (res.ok) {
                fetchData();
            } else {
                const err = await res.json();
                alert(err.detail);
            }
        } catch (error) {
            console.error('Add guardian error', error);
        } finally {
            setActionLoading(null);
        }
    };

    if (loading) {
        return (
            <div className="p-8 flex justify-center">
                <FaSpinner className="animate-spin text-primary text-2xl" />
            </div>
        );
    }

    return (
        <div className="bg-white rounded-3xl overflow-hidden">
            <div className="p-6 border-b border-gray-100 flex items-center justify-between">
                <div className="flex items-center space-x-3">
                    <div className="w-10 h-10 bg-purple-50 text-primary rounded-xl flex items-center justify-center text-xl">
                        <FaShieldHalved />
                    </div>
                    <div>
                        <h2 className="font-bold text-lg">Social Recovery</h2>
                        <p className="text-secondary text-xs">{guardians.length}/7 Guardians selected</p>
                    </div>
                </div>
                {onClose && (
                    <button onClick={onClose} className="text-secondary hover:text-foreground p-2">
                        Close
                    </button>
                )}
            </div>

            <div className="p-6 space-y-6">
                <div className="bg-blue-50/50 p-4 rounded-2xl flex items-start space-x-3">
                    <FaCircleInfo className="text-primary mt-1" />
                    <p className="text-xs text-secondary leading-relaxed">
                        Select up to 7 trusted contacts as guardians. If you lose access to your device, a majority (4/7) can help you recover your account identity.
                    </p>
                </div>

                {/* Current Guardians */}
                <div className="space-y-3">
                    <h3 className="text-xs font-bold text-gray-400 uppercase tracking-widest">Active Guardians ({guardians.length})</h3>
                    {guardians.length === 0 ? (
                        <div className="py-8 text-center border-2 border-dashed border-gray-100 rounded-3xl">
                            <p className="text-sm text-secondary">No guardians added yet.</p>
                        </div>
                    ) : (
                        <div className="space-y-2">
                            {guardians.map((gid) => {
                                const peer = contacts.find(c => c.peer_id === gid);
                                return (
                                    <div key={gid} className="flex items-center justify-between p-3 bg-gray-50 rounded-2xl">
                                        <div className="flex items-center space-x-3">
                                            <div className="w-8 h-8 bg-white rounded-full flex items-center justify-center text-xs font-bold shadow-sm">
                                                {peer?.username?.[0] || 'U'}
                                            </div>
                                            <div>
                                                <p className="text-sm font-bold">{peer?.username || `Peer ${gid.substring(0, 8)}...`}</p>
                                                <p className="text-[10px] text-secondary font-mono truncate max-w-[150px]">{gid}</p>
                                            </div>
                                        </div>
                                        <div className="flex items-center text-green-500">
                                            <FaCircleCheck className="text-sm" />
                                        </div>
                                    </div>
                                );
                            })}
                        </div>
                    )}
                </div>

                {/* Potential Guardians (Contacts) */}
                <div className="space-y-3">
                    <h3 className="text-xs font-bold text-gray-400 uppercase tracking-widest">Available Contacts</h3>
                    <div className="space-y-2 max-h-[200px] overflow-y-auto pr-2">
                        {contacts.filter(c => !guardians.includes(c.peer_id)).length === 0 ? (
                            <p className="text-xs text-secondary text-center py-4">Add more trusted contacts to select guardians.</p>
                        ) : (
                            contacts.filter(c => !guardians.includes(c.peer_id)).map((peer) => (
                                <div key={peer.peer_id} className="flex items-center justify-between p-3 border border-gray-100 rounded-2xl hover:border-primary/30 transition-all">
                                    <div className="flex items-center space-x-3">
                                        <div className="w-8 h-8 bg-gray-100 rounded-full flex items-center justify-center text-xs font-bold">
                                            {peer.username[0]}
                                        </div>
                                        <div>
                                            <p className="text-sm font-bold">{peer.username}</p>
                                            <p className="text-[10px] text-secondary">Trusted Contact</p>
                                        </div>
                                    </div>
                                    <button
                                        onClick={() => addGuardian(peer.peer_id)}
                                        disabled={actionLoading === peer.peer_id || guardians.length >= 7}
                                        className="p-2 bg-primary text-white rounded-xl hover:bg-primary/90 disabled:opacity-50"
                                    >
                                        {actionLoading === peer.peer_id ? <FaSpinner className="animate-spin" /> : <FaUserPlus />}
                                    </button>
                                </div>
                            ))
                        )}
                    </div>
                </div>
            </div>

            {guardians.length >= 4 && (
                <div className="p-6 bg-green-50 mt-4 text-center">
                    <p className="text-xs font-bold text-green-700">✓ Recovery Threshold Met</p>
                    <p className="text-[10px] text-green-600">You have enough guardians to secure your account.</p>
                </div>
            )}
        </div>
    );
}
