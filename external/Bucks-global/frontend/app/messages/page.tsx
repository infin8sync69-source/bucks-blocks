"use client";

import React, { useState, useEffect } from 'react';
import Link from 'next/link';
import { FaMessage, FaRegClock, FaUser, FaPlus } from 'react-icons/fa6';
import NewChatModal from '@/components/NewChatModal';
import api from '@/lib/api';

interface Conversation {
    peer_id: string;
    last_message: string;
    timestamp: string;
    unread_count: number;
    username?: string; // We'll try to resolve this from following list
}

export default function MessagesDashboard() {
    const [conversations, setConversations] = useState<Conversation[]>([]);
    const [following, setFollowing] = useState<any[]>([]);
    const [loading, setLoading] = useState(true);
    const [isModalOpen, setIsModalOpen] = useState(false);

    useEffect(() => {
        const fetchConversations = async () => {
            try {
                const response = await api.get('/messages');
                const data = response.data;

                // Fetch following list to map usernames AND for new chat suggestions
                const followResponse = await api.get('/following');
                const followData = followResponse.data;
                const followingList = followData.following || [];
                setFollowing(followingList);

                const mappedConversations = data.conversations.map((conv: Conversation) => {
                    const peer = followingList.find((f: any) => f.peer_id === conv.peer_id);
                    return {
                        ...conv,
                        username: peer ? peer.username : `Peer ${conv.peer_id.substring(0, 8)}...`
                    };
                });

                setConversations(mappedConversations);
            } catch (error) {
                console.error('Failed to fetch conversations', error);
            } finally {
                setLoading(false);
            }
        };

        fetchConversations();
    }, []);

    if (loading) {
        return (
            <div className="flex items-center justify-center min-h-[50vh]">
                <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary"></div>
            </div>
        );
    }

    return (
        <div className="max-w-2xl mx-auto p-4 md:p-6 pb-24">
            <div className="flex items-center justify-between mb-6">
                <div className="flex items-center space-x-3">
                    <FaMessage className="text-2xl text-primary" />
                    <h1 className="text-2xl font-bold">Messages</h1>
                </div>
                <button
                    onClick={() => setIsModalOpen(true)}
                    className="w-10 h-10 bg-primary/10 hover:bg-primary/20 text-primary rounded-full flex items-center justify-center transition-colors"
                >
                    <FaPlus className="text-lg" />
                </button>
            </div>

            {conversations.length === 0 ? (
                <div className="text-center py-20 bg-white rounded-2xl border border-gray-100 shadow-sm">
                    <div className="w-16 h-16 bg-gray-50 rounded-full flex items-center justify-center mx-auto mb-4">
                        <FaMessage className="text-2xl text-gray-300" />
                    </div>
                    <p className="text-gray-600 font-medium">No messages yet</p>
                    <p className="text-sm text-gray-400 mt-1">Start a conversation from a user's profile</p>
                    <button
                        onClick={() => setIsModalOpen(true)}
                        className="inline-block mt-6 px-6 py-2 bg-primary text-white rounded-full text-sm font-medium hover:bg-primary/90 transition-colors"
                    >
                        Start New Chat
                    </button>
                </div>
            ) : (
                <div className="space-y-2">
                    {conversations.map((conv) => (
                        <Link
                            key={conv.peer_id}
                            href={`/messages/${conv.peer_id}`}
                            className="flex items-center p-4 bg-white rounded-2xl border border-gray-100 hover:border-primary/30 hover:shadow-sm transition-all group"
                        >
                            <div className="w-12 h-12 rounded-full bg-gray-100 flex items-center justify-center text-xl mr-4 group-hover:bg-primary/5 transition-colors">
                                <FaUser className="text-gray-400 group-hover:text-primary transition-colors" />
                            </div>
                            <div className="flex-1 min-w-0">
                                <div className="flex items-center justify-between mb-1">
                                    <h3 className="font-semibold text-foreground truncate">{conv.username}</h3>
                                    <span className="text-[10px] text-secondary flex items-center">
                                        <FaRegClock className="mr-1" />
                                        {new Date(conv.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                                    </span>
                                </div>
                                <p className="text-sm text-secondary truncate">{conv.last_message}</p>
                            </div>
                            {conv.unread_count > 0 && (
                                <div className="ml-3 w-5 h-5 bg-primary rounded-full flex items-center justify-center text-[10px] text-white font-bold">
                                    {conv.unread_count}
                                </div>
                            )}
                        </Link>
                    ))}
                </div>
            )}

            <NewChatModal
                isOpen={isModalOpen}
                onClose={() => setIsModalOpen(false)}
                following={following}
            />
        </div>
    );
}
