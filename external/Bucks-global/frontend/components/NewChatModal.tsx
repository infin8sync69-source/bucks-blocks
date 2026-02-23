"use client";

import React, { useState, useEffect } from 'react';
import { FaXmark, FaUserPlus, FaSpinner } from 'react-icons/fa6';
import SearchInput from './SearchInput';
import Avatar from './Avatar';
import Link from 'next/link';

interface User {
    peer_id: string;
    username: string;
    handle: string;
    avatar?: string;
    bio?: string;
}

interface NewChatModalProps {
    isOpen: boolean;
    onClose: () => void;
    following: any[]; // User list from parent
}

const NewChatModal: React.FC<NewChatModalProps> = ({ isOpen, onClose, following }) => {
    const [query, setQuery] = useState('');
    const [results, setResults] = useState<User[]>([]);
    const [isSearching, setIsSearching] = useState(false);

    // Initialize results with following list when modal opens or query is empty
    useEffect(() => {
        if (!isOpen) {
            setQuery('');
            setResults([]);
            return;
        }

        if (!query.trim()) {
            setResults(following.map(f => ({
                peer_id: f.peer_id,
                username: f.username,
                handle: f.handle || '',
                avatar: f.avatar,
                bio: f.bio
            })));
        }
    }, [isOpen, query, following]);

    const handleSearch = async (searchQuery: string) => {
        setQuery(searchQuery);

        if (!searchQuery.trim()) {
            setResults(following.map(f => ({
                peer_id: f.peer_id,
                username: f.username,
                handle: f.handle || '',
                avatar: f.avatar,
                bio: f.bio
            })));
            return;
        }

        setIsSearching(true);
        try {
            const host = typeof window !== 'undefined' ? window.location.hostname : 'localhost';
            // Use existing search API
            const response = await fetch(`http://${host}:8000/api/search`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ query: searchQuery })
            });
            const data = await response.json();

            // Filter only users and map to User interface
            const userResults = (data.results || [])
                .filter((r: any) => r.type === 'user')
                .map((r: any) => ({
                    peer_id: r.peer_id,
                    username: r.name,
                    handle: r.author,
                    avatar: r.avatar,
                    bio: r.description
                }));

            setResults(userResults);
        } catch (error) {
            console.error("Search failed", error);
        } finally {
            setIsSearching(false);
        }
    };

    if (!isOpen) return null;

    return (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/50 backdrop-blur-sm animate-in fade-in duration-200">
            <div className="bg-white rounded-3xl w-full max-w-md shadow-2xl overflow-hidden animate-in zoom-in-95 duration-200">
                {/* Header */}
                <div className="flex items-center justify-between p-4 border-b border-gray-100">
                    <h3 className="text-lg font-bold flex items-center">
                        <FaUserPlus className="mr-2 text-primary" />
                        New Chat
                    </h3>
                    <button onClick={onClose} className="p-2 hover:bg-gray-100 rounded-full transition-colors">
                        <FaXmark className="text-gray-500" />
                    </button>
                </div>

                {/* Search */}
                <div className="p-4 bg-gray-50/50">
                    <SearchInput
                        placeholder="Search for people..."
                        onSearch={handleSearch}
                        initialQuery={query}
                        className="shadow-sm bg-white"
                    />
                </div>

                {/* Results List */}
                <div className="max-h-[60vh] overflow-y-auto p-2">
                    {isSearching ? (
                        <div className="flex items-center justify-center py-12 text-primary">
                            <FaSpinner className="animate-spin text-2xl" />
                        </div>
                    ) : results.length > 0 ? (
                        <div className="space-y-1">
                            {results.map((user) => (
                                <Link
                                    key={user.peer_id}
                                    href={`/messages/${user.peer_id}`}
                                    onClick={onClose} // Close modal on selection
                                    className="flex items-center p-3 hover:bg-gray-50 rounded-xl transition-colors group"
                                >
                                    <Avatar src={user.avatar} className="w-10 h-10 mr-3" />
                                    <div className="flex-1 min-w-0">
                                        <div className="flex items-center justify-between">
                                            <h4 className="font-semibold text-gray-900 truncate group-hover:text-primary transition-colors">
                                                {user.username}
                                            </h4>
                                            {following.some(f => f.peer_id === user.peer_id) && (
                                                <span className="text-[10px] bg-gray-100 text-gray-500 px-2 py-0.5 rounded-full">Following</span>
                                            )}
                                        </div>
                                        <p className="text-xs text-gray-500 truncate">@{user.handle}</p>
                                    </div>
                                </Link>
                            ))}
                        </div>
                    ) : (
                        <div className="text-center py-12">
                            <p className="text-gray-400 font-medium">No users found</p>
                            {query && <p className="text-xs text-gray-300 mt-1">Try a different search term</p>}
                        </div>
                    )}
                </div>
            </div>
        </div>
    );
};

export default NewChatModal;
