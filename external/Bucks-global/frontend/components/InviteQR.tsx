"use client";

import React, { useState, useEffect } from 'react';
import { FaQrcode, FaCopy, FaShareAlt, FaWifi } from 'react-icons/fa';
import { useToast } from './Toast';

interface InviteQRProps {
    peerId?: string;
    did?: string;
    className?: string;
}

const InviteQR: React.FC<InviteQRProps> = ({ peerId, did, className = "" }) => {
    const { showToast } = useToast();
    const [localIp, setLocalIp] = useState("localhost");

    useEffect(() => {
        const getNetworkInfo = async () => {
            try {
                const response = await fetch(`${window.location.origin}/api/network-info`);
                const data = await response.json();
                if (data.ip) {
                    setLocalIp(data.ip);
                }
            } catch (err) {
                console.error("Failed to fetch network info", err);
                // Fallback to current hostname
                if (typeof window !== 'undefined' && window.location.hostname !== 'localhost') {
                    setLocalIp(window.location.hostname);
                }
            }
        };
        getNetworkInfo();
    }, []);

    const inviteUrl = `http://${localIp}:3000/profile/${peerId || did}`;

    const copyToClipboard = () => {
        navigator.clipboard.writeText(inviteUrl);
        showToast("Invite link copied to clipboard!", "success");
    };

    return (
        <div className={`p-6 bg-gradient-to-br from-gray-900 to-black rounded-3xl border border-white/10 text-white shadow-2xl ${className}`}>
            <div className="flex items-center justify-between mb-6">
                <div className="flex items-center space-x-2">
                    <div className="w-8 h-8 bg-primary rounded-lg flex items-center justify-center text-sm shadow-lg shadow-primary/20">
                        <FaWifi />
                    </div>
                    <span className="font-bold text-sm tracking-tight uppercase opacity-60">WiFi Sync Invite</span>
                </div>
                <div className="px-3 py-1 bg-green-500/10 border border-green-500/20 rounded-full flex items-center">
                    <div className="w-1.5 h-1.5 bg-green-500 rounded-full animate-pulse mr-2" />
                    <span className="text-[10px] font-bold text-green-500 uppercase">Local Node Active</span>
                </div>
            </div>

            <div className="flex flex-col items-center">
                {/* Simulated QR Code (Real one would use a library like qrcode.react) */}
                <div className="w-48 h-48 bg-white p-3 rounded-2xl mb-6 shadow-[0_0_50px_rgba(255,255,255,0.1)] group relative cursor-pointer overflow-hidden transition-transform hover:scale-105 active:scale-95">
                    <div className="w-full h-full bg-gray-50 flex items-center justify-center border-4 border-dashed border-gray-200 rounded-xl">
                        <FaQrcode className="text-6xl text-gray-300 group-hover:text-primary transition-colors" />
                    </div>
                    <div className="absolute inset-0 bg-primary/80 opacity-0 group-hover:opacity-100 flex flex-col items-center justify-center transition-opacity">
                        <FaShareAlt className="text-3xl text-white mb-2" />
                        <span className="text-xs font-bold text-white uppercase tracking-wider">Expand QR</span>
                    </div>
                </div>

                <div className="w-full space-y-3">
                    <div className="p-3 bg-white/5 border border-white/10 rounded-xl">
                        <p className="text-[10px] font-bold text-gray-500 uppercase tracking-widest mb-1">Local Address</p>
                        <p className="text-xs font-mono truncate text-blue-300">{inviteUrl}</p>
                    </div>

                    <div className="flex space-x-2">
                        <button
                            onClick={copyToClipboard}
                            className="flex-1 py-3 bg-white/10 border border-white/5 hover:bg-white/20 rounded-xl font-bold text-xs transition-all flex items-center justify-center space-x-2"
                        >
                            <FaCopy />
                            <span>Copy Link</span>
                        </button>
                    </div>
                </div>

                <p className="mt-6 text-[10px] text-gray-500 text-center leading-relaxed">
                    Ask friends on the <span className="text-white font-bold">same WiFi</span> to scan this code to sync with your feed.
                </p>
            </div>
        </div>
    );
};

export default InviteQR;
