"use client";

import React, { useState, useRef, useEffect } from 'react';
import Link from 'next/link';
import { FaMagnifyingGlass, FaPaperPlane } from 'react-icons/fa6';
import StarField from '@/components/StarField';
import api from '@/lib/api';

interface Message {
    role: 'user' | 'assistant';
    content: string;
    timestamp: string;
    results?: SearchResult[];
}

interface SearchResult {
    cid: string;
    name: string;
    description: string;
    author: string;
    filename: string;
}

export default function Home() {
    const [query, setQuery] = useState('');
    const [messages, setMessages] = useState<Message[]>([
        {
            role: 'assistant',
            content: 'Welcome! I can help you search for images, videos, and content across the IPFS network. What are you looking for?',
            timestamp: ''
        }
    ]);
    const [isSearching, setIsSearching] = useState(false);
    const messagesEndRef = useRef<HTMLDivElement>(null);

    const scrollToBottom = () => {
        messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
    };

    const [mounted, setMounted] = useState(false);

    useEffect(() => {
        setMounted(true);
        scrollToBottom();
    }, [messages]);

    if (!mounted) return null; // Avoid hydration mismatch for dynamic content

    const handleSearch = async (e: React.FormEvent) => {
        e.preventDefault();
        if (!query.trim()) return;

        const userMessage: Message = {
            role: 'user',
            content: query,
            timestamp: new Date().toLocaleTimeString()
        };

        setMessages(prev => [...prev, userMessage]);
        setQuery('');
        setIsSearching(true);

        try {
            // Check for Agent Commands (starting with /)
            if (query.startsWith('/')) {
                // Import dynamically to avoid server-side issues if any
                const { fetchAgentResponse } = await import('@/lib/api');
                const responseText = await fetchAgentResponse(query);

                const assistantMessage: Message = {
                    role: 'assistant',
                    content: responseText,
                    timestamp: new Date().toLocaleTimeString()
                };

                setMessages(prev => [...prev, assistantMessage]);
                setIsSearching(false);
                return;
            }

            // Standard IPFS Search
            const response = await api.post('/search', { query });
            const data = response.data;
            const results: SearchResult[] = data.results || [];

            let assistantResponse = '';
            if (results.length === 0) {
                assistantResponse = `I couldn't find any content matching "${query}". Try searching for something else or uploading new content!`;
            } else {
                assistantResponse = `I found ${results.length} result${results.length > 1 ? 's' : ''} for "${query}":\n\n`;
                results.slice(0, 5).forEach((result, i) => {
                    assistantResponse += `${i + 1}. **${result.name}** by ${result.author}\n   📁 ${result.filename}\n   CID: \`${result.cid.substring(0, 20)}...\`\n\n`;
                });
                if (results.length > 5) {
                    assistantResponse += `...and ${results.length - 5} more results.`;
                }
            }

            const assistantMessage: Message = {
                role: 'assistant',
                content: assistantResponse,
                timestamp: new Date().toLocaleTimeString(),
                results: results.slice(0, 5)
            };

            setTimeout(() => {
                setMessages(prev => [...prev, assistantMessage]);
                setIsSearching(false);
            }, 500);

        } catch (error) {
            console.error('Search error:', error);
            const errorMessage: Message = {
                role: 'assistant',
                content: `Error: ${error instanceof Error ? error.message : 'Unknown error occurred'}`,
                timestamp: new Date().toLocaleTimeString()
            };
            setMessages(prev => [...prev, errorMessage]);
            setIsSearching(false);
        }
    };

    return (
        <div className="relative min-h-screen w-full overflow-hidden">
            <StarField />
            {/* Overlay for better readability */}
            <div className="fixed inset-0 bg-black/30 pointer-events-none" style={{ zIndex: -14 }} />

            {/* Header Glow */}
            <div className="fixed top-0 left-1/2 -translate-x-1/2 w-full md:w-[1000px] h-[400px] bg-primary/20 blur-[150px] rounded-full pointer-events-none" style={{ zIndex: -13 }} />

            {/* Chat Container */}
            <div className="flex flex-col min-h-screen max-w-4xl mx-auto px-4 pt-24 pb-8 relative z-10">
                {/* Messages Area */}
                <div className="flex-1 overflow-y-auto mb-6 space-y-6 scrollbar-hide pb-32">
                    {messages.map((message, index) => (
                        <div
                            key={index}
                            className={`flex ${message.role === 'user' ? 'justify-end' : 'justify-start'} animate-fade-in`}
                        >
                            <div
                                className={`max-w-[85%] md:max-w-[75%] px-6 py-4 rounded-3xl shadow-2xl transition-all ${message.role === 'user'
                                    ? 'bg-gradient-to-br from-primary via-purple-600 to-blue-600 text-white border border-white/10'
                                    : 'bg-white/5 backdrop-blur-xl text-white border border-white/20'
                                    }`}
                            >
                                <p className="text-sm whitespace-pre-line">{message.content}</p>
                                {message.results && message.results.length > 0 && (
                                    <div className="mt-4 space-y-2">
                                        {message.results.map((result) => (
                                            <Link
                                                key={result.cid}
                                                href={`/feed?cid=${result.cid}`}
                                                className="block p-3 rounded-xl bg-white/10 hover:bg-white/20 border border-white/10 transition-all group"
                                            >
                                                <div className="flex items-center justify-between">
                                                    <div className="min-w-0 flex-1">
                                                        <p className="text-sm font-bold truncate group-hover:text-purple-300 transition-colors">{result.name}</p>
                                                        <p className="text-[10px] opacity-60 truncate">Shared by {result.author}</p>
                                                    </div>
                                                    <div className="ml-2 w-8 h-8 rounded-lg bg-white/10 flex items-center justify-center text-[10px] font-bold">
                                                        View
                                                    </div>
                                                </div>
                                            </Link>
                                        ))}
                                    </div>
                                )}
                                <span className="text-xs opacity-60 mt-1 block" suppressHydrationWarning>{message.timestamp}</span>
                            </div>
                        </div>
                    ))}
                    {isSearching && (
                        <div className="flex justify-start animate-fade-in">
                            <div className="glass text-white border border-white/20 px-5 py-3 rounded-2xl">
                                <div className="flex space-x-2">
                                    <div className="w-2 h-2 bg-white rounded-full animate-bounce" style={{ animationDelay: '0s' }}></div>
                                    <div className="w-2 h-2 bg-white rounded-full animate-bounce" style={{ animationDelay: '0.2s' }}></div>
                                    <div className="w-2 h-2 bg-white rounded-full animate-bounce" style={{ animationDelay: '0.4s' }}></div>
                                </div>
                            </div>
                        </div>
                    )}
                    <div ref={messagesEndRef} />
                </div>

                {/* Search Input */}
                <form onSubmit={handleSearch} className="relative group">
                    <div className="bg-white/10 backdrop-blur-2xl border border-white/20 rounded-3xl p-2.5 flex items-center space-x-4 shadow-[0_20px_50px_rgba(0,0,0,0.5)] transition-all focus-within:border-primary/50 focus-within:ring-4 focus-within:ring-primary/10">
                        <div className="bg-primary/20 p-2.5 rounded-2xl ml-1">
                            <FaMagnifyingGlass className="text-primary text-lg" />
                        </div>
                        <input
                            type="text"
                            value={query}
                            onChange={(e) => setQuery(e.target.value)}
                            placeholder="Ask Bucks anything..."
                            className="flex-1 bg-transparent text-white placeholder-white/40 outline-none text-base py-3"
                            disabled={isSearching}
                        />
                        <button
                            type="submit"
                            disabled={!query.trim() || isSearching}
                            className="bg-primary hover:bg-primary-hover text-white px-8 py-3.5 rounded-2xl font-bold text-sm transition-all disabled:opacity-50 disabled:cursor-not-allowed flex items-center space-x-2 shadow-lg active:scale-95"
                        >
                            <FaPaperPlane className="text-xs" />
                            <span>Ask</span>
                        </button>
                    </div>
                </form>
            </div>
        </div>
    );
}
