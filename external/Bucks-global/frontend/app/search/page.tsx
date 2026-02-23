"use client";

import React, { useState, useEffect, Suspense } from 'react';
import { useSearchParams, useRouter } from 'next/navigation';
import SearchInput from '@/components/SearchInput';
import Avatar from '@/components/Avatar';
import api, { getIPFSUrl } from '@/lib/api';
import { FaUser, FaFile, FaFileImage, FaFileVideo, FaFileAudio, FaSpinner, FaUsers, FaLayerGroup } from 'react-icons/fa6';
import Link from 'next/link';

interface SearchResult {
    type: 'post' | 'user';
    cid: string; // peer_id for users, id for posts
    name: string; // username for users
    description: string; // bio for users
    author: string; // handle for users
    avatar?: string;
    filename?: string;
    timestamp?: string;
    peer_id?: string;
}

const SearchContent = () => {
    const searchParams = useSearchParams();
    const router = useRouter();
    const initialQuery = searchParams.get('q') || '';

    const [query, setQuery] = useState(initialQuery);
    const [activeTab, setActiveTab] = useState<'all' | 'posts' | 'people'>('all');
    const [results, setResults] = useState<SearchResult[]>([]);
    const [loading, setLoading] = useState(false);
    const [hasSearched, setHasSearched] = useState(false);

    useEffect(() => {
        if (initialQuery) {
            handleSearch(initialQuery);
        }
    }, [initialQuery]);

    const handleSearch = async (searchQuery: string) => {
        if (searchQuery !== query) {
            setQuery(searchQuery);
            router.replace(`/search?q=${encodeURIComponent(searchQuery)}`);
        }

        if (!searchQuery.trim()) {
            setResults([]);
            setHasSearched(false);
            return;
        }

        setLoading(true);
        setHasSearched(true);
        try {
            const response = await api.post('/search', { query: searchQuery });
            const data = response.data;
            setResults(data.results || []);
        } catch (error) {
            console.error("Search failed", error);
        } finally {
            setLoading(false);
        }
    };

    const filterResults = () => {
        if (activeTab === 'all') return results;
        return results.filter(r => r.type === (activeTab === 'people' ? 'user' : 'post'));
    };

    const getFileIcon = (filename: string) => {
        if (!filename) return <FaFile />;
        const ext = filename.split('.').pop()?.toLowerCase();
        if (['jpg', 'jpeg', 'png', 'gif', 'webp'].includes(ext || '')) return <FaFileImage />;
        if (['mp4', 'webm', 'mov'].includes(ext || '')) return <FaFileVideo />;
        if (['mp3', 'wav', 'ogg'].includes(ext || '')) return <FaFileAudio />;
        return <FaFile />;
    };

    const filtered = filterResults();

    return (
        <div className="bg-white min-h-screen pb-20">
            <div className="pt-20 px-4 max-w-2xl mx-auto">
                <div className="mb-6">
                    <SearchInput
                        initialQuery={query}
                        onSearch={handleSearch}
                        placeholder="Search posts, people, files..."
                        className="shadow-sm"
                    />
                </div>

                {/* Tabs */}
                {hasSearched && (
                    <div className="flex items-center space-x-1 mb-6 border-b border-gray-100 pb-1 overflow-x-auto scrollbar-hide">
                        {[
                            { id: 'all', label: 'All', icon: <FaLayerGroup /> },
                            { id: 'posts', label: 'Posts', icon: <FaFile /> },
                            { id: 'people', label: 'People', icon: <FaUsers /> }
                        ].map((tab) => (
                            <button
                                key={tab.id}
                                onClick={() => setActiveTab(tab.id as any)}
                                className={`flex items-center space-x-2 px-4 py-2.5 rounded-xl text-sm font-bold transition-all whitespace-nowrap ${activeTab === tab.id
                                    ? 'bg-primary text-white shadow-lg shadow-primary/20'
                                    : 'text-gray-500 hover:bg-gray-100'
                                    }`}
                            >
                                <span className={activeTab === tab.id ? '' : 'opacity-70'}>{tab.icon}</span>
                                <span>{tab.label}</span>
                            </button>
                        ))}
                    </div>
                )}

                {/* Results */}
                {loading ? (
                    <div className="flex flex-col items-center justify-center py-20 space-y-4">
                        <FaSpinner className="text-4xl text-primary animate-spin" />
                        <p className="text-gray-400 font-medium text-sm">Searching the swarm...</p>
                    </div>
                ) : (
                    <div className="space-y-4">
                        {hasSearched && filtered.length === 0 && (
                            <div className="text-center py-20">
                                <p className="text-gray-400 font-medium">No results found for "{query}"</p>
                            </div>
                        )}

                        {filtered.map((result, idx) => (
                            <div key={`${result.cid}-${idx}`} className="animate-in fade-in slide-in-from-bottom-4 duration-500" style={{ animationDelay: `${idx * 50}ms` }}>
                                {result.type === 'user' ? (
                                    <Link href={`/profile/${result.peer_id}`} className="block bg-white border border-gray-100 rounded-2xl p-4 hover:border-primary/30 hover:shadow-md transition-all group">
                                        <div className="flex items-center space-x-4">
                                            <Avatar seed={result.peer_id} src={result.avatar} size="lg" className="group-hover:scale-105 transition-transform" />
                                            <div>
                                                <h3 className="font-bold text-gray-900">{result.name || 'Anonymous'}</h3>
                                                <p className="text-xs text-primary font-medium">@{(result.author || 'unknown').replace(/^@/, '')}</p>
                                                <p className="text-sm text-gray-500 mt-1 line-clamp-1">{result.description}</p>
                                            </div>
                                        </div>
                                    </Link>
                                ) : (
                                    <Link href={`/feed?cid=${result.cid}`} className="block bg-white border border-gray-100 rounded-2xl p-4 hover:border-primary/30 hover:shadow-md transition-all group">
                                        <div className="flex items-start space-x-4">
                                            <div className="w-12 h-12 bg-gray-50 rounded-xl flex items-center justify-center text-gray-400 group-hover:text-primary group-hover:bg-primary/5 transition-colors">
                                                {getFileIcon(result.filename || '')}
                                            </div>
                                            <div className="flex-1 min-w-0">
                                                <h3 className="font-bold text-gray-900 truncate pr-4">{result.name}</h3>
                                                <p className="text-xs text-gray-400 font-medium mb-1">
                                                    by {result.author} • {new Date(result.timestamp || Date.now()).toLocaleDateString()}
                                                </p>
                                                <p className="text-sm text-gray-600 line-clamp-2">{result.description}</p>
                                                {result.filename && (
                                                    <div className="mt-2 inline-flex items-center px-2 py-1 bg-gray-50 rounded-lg text-[10px] text-gray-500 font-mono">
                                                        {result.filename}
                                                    </div>
                                                )}
                                            </div>
                                        </div>
                                    </Link>
                                )}
                            </div>
                        ))}
                    </div>
                )}
            </div>
        </div>
    );
};

export default function SearchPage() {
    return (
        <Suspense fallback={
            <div className="flex items-center justify-center min-h-screen">
                <FaSpinner className="text-4xl text-primary animate-spin" />
            </div>
        }>
            <SearchContent />
        </Suspense>
    );
}
