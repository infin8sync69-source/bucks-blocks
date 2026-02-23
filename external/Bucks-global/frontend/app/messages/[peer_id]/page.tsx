"use client";

import React, { useState, useEffect, useRef } from 'react';
import { useParams, useRouter } from 'next/navigation';
import { FaArrowLeft, FaPaperPlane, FaUser, FaCircle, FaMessage, FaPaperclip, FaXmark, FaFile, FaFilePdf } from 'react-icons/fa6';
import Link from 'next/link';
import { api, getIPFSUrl } from '@/lib/api';

interface Message {
    sender: string;
    text: string;
    timestamp: string;
    cid?: string;
    filename?: string;
    mime_type?: string;
}

export default function ChatPage() {
    const params = useParams();
    const router = useRouter();
    const peerId = params.peer_id as string;

    const [peerName, setPeerName] = useState('');
    const [loading, setLoading] = useState(true);
    const [sending, setSending] = useState(false);
    const [messages, setMessages] = useState<Message[]>([]);
    const [newMessage, setNewMessage] = useState('');
    const [myId, setMyId] = useState('');
    const [relationship, setRelationship] = useState<'sync' | 'contact' | 'none'>('none');

    // Media handling
    const [selectedFile, setSelectedFile] = useState<File | null>(null);
    const [previewUrl, setPreviewUrl] = useState<string | null>(null);
    const fileInputRef = useRef<HTMLInputElement>(null);

    const messagesEndRef = useRef<HTMLDivElement>(null);

    const scrollToBottom = () => {
        messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
    };

    const fetchChat = async () => {
        try {
            // Fetch messages
            const response = await api.get(`/messages/${peerId}`);
            setMessages(response.data.history || []);

            // Fetch profile for my ID
            const profileResponse = await api.get('/profile');
            setMyId(profileResponse.data.peer_id);

            // Fetch following to get peer status
            const followResponse = await api.get('/following');
            const peer = followResponse.data.following.find((f: any) => f.peer_id === peerId);

            if (peer) {
                setPeerName(peer.username);
                setRelationship(peer.relationship_type || 'sync');
            } else {
                setPeerName(`Peer ${peerId.substring(0, 8)}`);
                setRelationship('none');
            }
        } catch (error) {
            console.error('Failed to fetch chat', error);
        } finally {
            setLoading(false);
        }
    };

    useEffect(() => {
        fetchChat();
        // Polling for new messages (Simplified, real-time would use PubSub/WS)
        const interval = setInterval(fetchChat, 3000);
        return () => clearInterval(interval);
    }, [peerId]);

    useEffect(() => {
        scrollToBottom();
    }, [messages]);

    const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (file) {
            setSelectedFile(file);
            if (file.type.startsWith('image/')) {
                const url = URL.createObjectURL(file);
                setPreviewUrl(url);
            } else {
                setPreviewUrl(null);
            }
        }
    };

    const clearFile = () => {
        setSelectedFile(null);
        if (previewUrl) {
            URL.revokeObjectURL(previewUrl);
            setPreviewUrl(null);
        }
        if (fileInputRef.current) {
            fileInputRef.current.value = '';
        }
    };

    const handleSend = async (e: React.FormEvent) => {
        e.preventDefault();
        if ((!newMessage.trim() && !selectedFile) || sending) return;

        setSending(true);
        const formData = new FormData();
        formData.append('text', newMessage);
        formData.append('peer_id', peerId);
        if (selectedFile) {
            formData.append('file', selectedFile);
        }

        try {
            const response = await api.post(`/messages/send`, formData, {
                headers: { 'Content-Type': 'multipart/form-data' }
            });

            if (response.status === 200) {
                setNewMessage('');
                clearFile();
                fetchChat();
            }
        } catch (error) {
            console.error('Failed to send message', error);
        } finally {
            setSending(false);
        }
    };



    const renderMedia = (msg: Message) => {
        if (!msg.cid) return null;
        const cid = msg.cid;

        const isImage = msg.mime_type?.startsWith('image/') || /\.(jpg|jpeg|png|gif|webp)$/i.test(msg.filename || '');
        const isVideo = msg.mime_type?.startsWith('video/') || /\.(mp4|webm|ogg)$/i.test(msg.filename || '');
        const isPdf = msg.mime_type === 'application/pdf' || /\.pdf$/i.test(msg.filename || '');

        if (isImage) {
            return (
                <div className="mt-2 rounded-lg overflow-hidden border border-gray-100 bg-gray-50">
                    <img
                        src={getIPFSUrl(cid)}
                        alt={msg.filename || 'Image'}
                        className="max-w-full h-auto object-contain cursor-pointer hover:opacity-95 transition-opacity"
                        onClick={() => window.open(getIPFSUrl(cid), '_blank')}
                    />
                </div>
            );
        }

        if (isVideo) {
            return (
                <div className="mt-2 rounded-lg overflow-hidden border border-gray-100 bg-black">
                    <video
                        src={getIPFSUrl(cid)}
                        controls
                        className="max-w-full h-auto"
                    />
                </div>
            );
        }

        return (
            <div className="mt-2 flex items-center p-3 bg-white/10 rounded-xl border border-white/20 backdrop-blur-sm group hover:bg-white/20 transition-colors">
                <div className="w-10 h-10 bg-white/10 rounded-lg flex items-center justify-center mr-3">
                    {isPdf ? <FaFilePdf className="text-xl" /> : <FaFile className="text-xl" />}
                </div>
                <div className="flex-1 min-w-0 mr-3">
                    <p className="text-xs font-semibold truncate">{msg.filename || "Attachment"}</p>
                    <p className="text-[10px] opacity-70">IPFS Cloud</p>
                </div>
                <button
                    onClick={() => window.open(getIPFSUrl(cid), '_blank')}
                    className="p-2 bg-white/20 hover:bg-white/30 rounded-full transition-colors"
                >
                    <FaArrowLeft className="rotate-180 text-xs" />
                </button>
            </div>
        );
    };

    if (loading) {
        return (
            <div className="flex items-center justify-center min-h-screen">
                <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary"></div>
            </div>
        );
    }

    return (
        <div className="flex flex-col h-screen max-w-2xl mx-auto bg-white border-x border-gray-100">
            {/* Header */}
            <header className="flex items-center h-16 px-4 border-b border-gray-100 bg-white/80 backdrop-blur-md sticky top-0 z-10">
                <button onClick={() => router.back()} className="p-2 hover:bg-gray-100 rounded-full mr-2">
                    <FaArrowLeft className="text-secondary" />
                </button>
                <div className="flex items-center flex-1">
                    <div className="w-10 h-10 rounded-full bg-gray-100 flex items-center justify-center mr-3">
                        <FaUser className="text-gray-400" />
                    </div>
                    <div>
                        <h2 className="font-bold text-foreground leading-tight">{peerName}</h2>
                        <div className="flex items-center text-[10px] text-green-500 font-medium">
                            <FaCircle className="text-[6px] mr-1" /> Online
                        </div>
                    </div>
                </div>
            </header>

            {/* Chat Messages */}
            <main className="flex-1 overflow-y-auto p-4 space-y-4">
                {messages.length === 0 ? (
                    <div className="flex flex-col items-center justify-center h-full text-secondary opacity-50">
                        <FaMessage className="text-4xl mb-4" />
                        <p className="text-sm">No messages yet. Send a hello!</p>
                    </div>
                ) : (
                    messages.map((msg, idx) => {
                        const isMine = msg.sender === myId;
                        return (
                            <div key={idx} className={`flex ${isMine ? 'justify-end' : 'justify-start'}`}>
                                <div className={`max-w-[80%] px-4 py-2 rounded-2xl text-sm ${isMine
                                    ? 'bg-primary text-white rounded-br-none shadow-sm'
                                    : 'bg-gray-100 text-foreground rounded-bl-none'
                                    }`}>
                                    {renderMedia(msg)}
                                    {msg.text && <p className={msg.cid ? "mt-2" : ""}>{msg.text}</p>}
                                    <p className={`text-[9px] mt-1 ${isMine ? 'text-white/70' : 'text-secondary opacity-70'}`}>
                                        {new Date(msg.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                                    </p>
                                </div>
                            </div>
                        );
                    })
                )}
                <div ref={messagesEndRef} />
            </main>

            {/* Input Bar */}
            <footer className="p-4 border-t border-gray-100 bg-white pb-24 md:pb-6">
                {relationship !== 'contact' && (
                    <div className="mb-4 p-3 bg-blue-50 border border-blue-100 rounded-2xl flex flex-col items-center text-center animate-in slide-in-from-bottom-2 duration-500">
                        <p className="text-[11px] font-bold text-primary uppercase tracking-wider mb-1">Message Request</p>
                        <p className="text-xs text-secondary leading-relaxed">
                            {relationship === 'sync'
                                ? "You are syncing with this peer. Messages will be sent as a request until you connect as contacts."
                                : "You are not syncing with this peer. Content delivery may be restricted."}
                        </p>
                        <Link href="/qr-scan" className="mt-2 text-[10px] font-bold text-primary hover:underline">
                            Connect as Contact via QR →
                        </Link>
                    </div>
                )}

                {/* File Preview */}
                {selectedFile && (
                    <div className="mb-3 p-2 bg-gray-50 rounded-2xl border border-gray-100 flex items-center animate-in slide-in-from-bottom-2 duration-200">
                        {previewUrl ? (
                            <div className="w-12 h-12 rounded-lg overflow-hidden mr-3">
                                <img src={previewUrl} className="w-full h-full object-cover" />
                            </div>
                        ) : (
                            <div className="w-12 h-12 bg-primary/10 rounded-lg flex items-center justify-center mr-3 text-primary">
                                <FaFile />
                            </div>
                        )}
                        <div className="flex-1 min-w-0">
                            <p className="text-xs font-semibold truncate">{selectedFile.name}</p>
                            <p className="text-[10px] text-secondary">{(selectedFile.size / 1024).toFixed(1)} KB</p>
                        </div>
                        <button onClick={clearFile} className="p-2 hover:bg-gray-200 rounded-full transition-colors">
                            <FaXmark className="text-secondary" />
                        </button>
                    </div>
                )}

                <form onSubmit={handleSend} className="flex items-center space-x-2">
                    <button
                        type="button"
                        onClick={() => fileInputRef.current?.click()}
                        className="w-10 h-10 flex items-center justify-center text-secondary hover:bg-gray-100 rounded-full transition-colors"
                    >
                        <FaPaperclip className="text-lg" />
                    </button>
                    <input
                        type="file"
                        className="hidden"
                        ref={fileInputRef}
                        onChange={handleFileSelect}
                    />
                    <input
                        type="text"
                        value={newMessage}
                        onChange={(e) => setNewMessage(e.target.value)}
                        placeholder={relationship === 'contact' ? "Type a message..." : "Send message request..."}
                        className="flex-1 px-4 py-2 bg-gray-100 border-none rounded-full text-sm focus:ring-2 focus:ring-primary/20 outline-none"
                    />
                    <button
                        type="submit"
                        disabled={(!newMessage.trim() && !selectedFile) || sending}
                        className="w-10 h-10 bg-primary text-white rounded-full flex items-center justify-center disabled:opacity-50 hover:bg-primary/90 transition-colors shadow-lg shadow-primary/20"
                    >
                        <FaPaperPlane className="text-sm transform -rotate-12 translate-x-0.5" />
                    </button>
                </form>
            </footer>
        </div>
    );
}
