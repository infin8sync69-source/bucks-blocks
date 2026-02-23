"use client";

import React, { useEffect, useState } from 'react';
import PostCard from '@/components/PostCard';
import { LibraryItem, api } from '@/lib/api';
import { FaMagnifyingGlass, FaFire, FaListUl, FaVideo, FaFileLines, FaHeart } from 'react-icons/fa6';
import DiscoveryGuide from '@/components/DiscoveryGuide';

export default function Recommended() {
    const [library, setLibrary] = useState<LibraryItem[]>([]);
    const [loading, setLoading] = useState(true);
    const [filter, setFilter] = useState<'all' | 'trending' | 'posts' | 'videos'>('all');
    const [searchQuery, setSearchQuery] = useState('');

    useEffect(() => {
        const loadRecommended = async () => {
            try {
                const response = await api.get<{ library: LibraryItem[] }>('/feed/recommended');
                setLibrary(response.data.library || []);
            } catch (error) {
                console.error('Failed to load recommended feed', error);
            } finally {
                setLoading(false);
            }
        };

        loadRecommended();
    }, []);

    const filteredLibrary = library.filter(item => {
        if (searchQuery && !item.name.toLowerCase().includes(searchQuery.toLowerCase()) && !item.description.toLowerCase().includes(searchQuery.toLowerCase())) {
            return false;
        }

        if (filter === 'videos') {
            return item.filename.match(/\.(mp4|webm|mov)$/i);
        }
        if (filter === 'posts') {
            return !item.filename.match(/\.(mp4|webm|mov|jpg|jpeg|png|gif|pdf)$/i);
        }
        // Trending logic could be refined based on recommendation count
        return true;
    });

    if (loading) {
        return (
            <div className="flex items-center justify-center min-h-[60vh]">
                <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary"></div>
            </div>
        );
    }

    return (
        <div className="flex flex-col min-h-screen bg-gray-50 pb-20">
            {/* Page Header */}
            <div className="bg-white px-4 py-3 pb-2 flex items-center space-x-2.5">
                <FaHeart className="text-gray-500 text-lg" />
                <h1 className="text-lg font-bold text-gray-800">Recommended</h1>
            </div>

            {/* Search Bar */}
            <div className="sticky top-0 z-20 bg-white px-4 py-3 border-b border-gray-100 shadow-sm">
                <div className="relative">
                    <FaMagnifyingGlass className="absolute left-3 top-1/2 -translate-y-1/2 text-gray-400 text-sm" />
                    <input
                        type="text"
                        placeholder="Search recommended..."
                        value={searchQuery}
                        onChange={(e) => setSearchQuery(e.target.value)}
                        className="w-full bg-gray-100 border-none rounded-2xl py-2.5 pl-10 pr-4 text-sm focus:ring-2 focus:ring-primary/20 transition-all outline-none"
                    />
                </div>

                {/* Filter Pills */}
                <div className="flex items-center space-x-2 mt-4 overflow-x-auto no-scrollbar pb-1">
                    {[
                        { id: 'all', label: 'All', icon: <FaListUl /> },
                        { id: 'trending', label: 'Trending', icon: <FaFire /> },
                        { id: 'posts', label: 'Posts', icon: <FaFileLines /> },
                        { id: 'videos', label: 'Videos', icon: <FaVideo /> },
                    ].map((btn) => (
                        <button
                            key={btn.id}
                            onClick={() => setFilter(btn.id as any)}
                            className={`flex items-center space-x-1.5 px-4 py-1.5 rounded-full text-xs font-bold whitespace-nowrap transition-all ${filter === btn.id
                                ? 'bg-primary text-white shadow-md shadow-primary/20 scale-105'
                                : 'bg-white text-gray-500 border border-gray-200 hover:border-gray-300'
                                }`}
                        >
                            <span className="text-[10px]">{btn.icon}</span>
                            <span>{btn.label}</span>
                        </button>
                    ))}
                </div>
            </div>

            {/* Content List */}
            <div className="flex-1 max-w-2xl mx-auto w-full mt-2">
                {filteredLibrary.length === 0 ? (
                    <div className="px-4 py-10">
                        <DiscoveryGuide />
                    </div>
                ) : (
                    <>
                        {filteredLibrary.map((item) => (
                            <PostCard key={item.cid} item={item} />
                        ))}

                        {filteredLibrary.length > 5 && (
                            <div className="p-6 text-center">
                                <button className="px-8 py-2.5 border-2 border-primary text-primary rounded-full text-sm font-bold hover:bg-primary/5 transition-colors">
                                    See more
                                </button>
                            </div>
                        )}
                    </>
                )}
            </div>
        </div>
    );
}
