"use client";

import React, { useEffect, useState, useMemo } from 'react';
import Link from 'next/link';
import { useRouter, useParams } from 'next/navigation';
import {
    FaUser, FaShare, FaMessage, FaEllipsisVertical,
    FaTableCells, FaList, FaUserGroup, FaShieldHalved,
    FaCopy, FaCheck, FaImages, FaFileLines, FaArrowsRotate,
    FaHeart, FaArrowLeft, FaSpinner
} from 'react-icons/fa6';
import {
    fetchUserProfile, fetchUserFeed, UserProfile, LibraryItem,
    fetchAllInteractions, Interaction, getIPFSUrl, fetchUserLikes,
    fetchFollowing, followPeer, unfollowPeer
} from '@/lib/api';
import PostCard from '@/components/PostCard';
import Avatar from '@/components/Avatar';
import { useToast } from '@/components/Toast';
import FormattedDate from '@/components/FormattedDate';

type Tab = 'feed' | 'media' | 'files' | 'recommended';

export default function UserProfilePage() {
    const params = useParams();
    const router = useRouter();
    const { showToast } = useToast();
    const [profile, setProfile] = useState<UserProfile | null>(null);
    const [activeTab, setActiveTab] = useState<Tab>('feed');
    const [library, setLibrary] = useState<LibraryItem[]>([]);
    const [interactions, setInteractions] = useState<Record<string, Interaction>>({});
    const [userLikes, setUserLikes] = useState<string[]>([]);
    const [loading, setLoading] = useState(true);

    // Sync (Follow) state
    const [isSynced, setIsSynced] = useState(false);
    const [syncLoading, setSyncLoading] = useState(false);
    const [syncChecked, setSyncChecked] = useState(false);

    const peerId = typeof params.id === 'string' ? params.id : '';

    useEffect(() => {
        if (!peerId) return;

        const loadData = async () => {
            setLoading(true);
            try {
                const [prof, feed, interact] = await Promise.all([
                    fetchUserProfile(peerId),
                    fetchUserFeed(peerId),
                    fetchAllInteractions()
                ]);

                setProfile(prof);
                setLibrary(feed);
                setInteractions(interact);

                // Check if already synced
                try {
                    const following = await fetchFollowing();
                    const alreadySynced = following?.some(
                        (f: any) => f.following_peer_id === peerId
                    );
                    setIsSynced(!!alreadySynced);
                } catch (e) {
                    console.error("Failed to check sync status", e);
                }
                setSyncChecked(true);

                // Fetch likes for Recommended tab
                fetchUserLikes(peerId).then(setUserLikes).catch(console.error);

            } catch (e) {
                console.error("Failed to load profile data", e);
                showToast('Failed to load profile', 'error');
            } finally {
                setLoading(false);
            }
        };
        loadData();
    }, [peerId]);

    // Handle Sync/Unsync
    const handleSync = async () => {
        if (syncLoading) return;
        setSyncLoading(true);
        try {
            if (isSynced) {
                await unfollowPeer(peerId);
                setIsSynced(false);
                showToast('Unsynced', 'success');
            } else {
                await followPeer(peerId);
                setIsSynced(true);
                showToast('Synced! You can now see their posts.', 'success');
            }
        } catch (e: any) {
            const msg = e?.response?.data?.detail || 'Sync failed';
            showToast(msg, 'error');
        } finally {
            setSyncLoading(false);
        }
    };

    if (loading) {
        return (
            <div className="min-h-screen flex items-center justify-center bg-gray-50">
                <div className="animate-spin rounded-full h-12 w-12 border-b-2 border-primary"></div>
            </div>
        );
    }

    if (!profile) {
        return (
            <div className="min-h-screen flex flex-col items-center justify-center bg-gray-50">
                <h2 className="text-xl font-bold text-gray-800">User not found</h2>
                <button onClick={() => router.back()} className="mt-4 text-primary hover:underline">Go Back</button>
            </div>
        );
    }

    // --- Content Filtering Logic ---
    const myPosts = library;
    const mediaPosts = myPosts.filter(p =>
        p.filename && (p.filename.toLowerCase().match(/\.(jpg|jpeg|png|gif|webp|mp4|mov|webm)$/i))
    );
    const filePosts = myPosts.filter(p => !mediaPosts.includes(p));

    // Recommended: posts this user has liked (CIDs in userLikes that exist in the global library)
    // Since we may not have the full post metadata for liked posts,
    // we show liked posts that are also in the library as a start
    const recommendedPosts = myPosts.filter(p => userLikes.includes(p.cid));

    return (
        <div className="bg-white min-h-screen pb-20 md:pb-0">
            {/* --- HEADER SECTION --- */}
            <div className="relative mb-16">
                {/* Back Button */}
                <button onClick={() => router.back()} className="absolute top-4 left-4 z-20 w-10 h-10 bg-black/20 backdrop-blur-md rounded-full flex items-center justify-center text-white hover:bg-black/30 transition-colors">
                    <FaArrowLeft />
                </button>

                {/* Banner */}
                <div className="h-32 md:h-56 bg-gradient-to-r from-gray-200 to-gray-300 w-full overflow-hidden relative">
                    <img
                        src={profile.banner && profile.banner.length > 0 ? profile.banner : "https://images.unsplash.com/photo-1614850523060-8da1d56ae167?q=80&w=2070&auto=format&fit=crop"}
                        alt="Banner"
                        className="w-full h-full object-cover"
                    />
                    <div className="absolute inset-0 bg-black/10"></div>
                </div>

                {/* Avatar & Actions Row */}
                <div className="absolute -bottom-12 left-0 right-0 px-4 flex justify-between items-end">
                    <div className="relative">
                        <Avatar
                            src={profile.avatar}
                            seed={profile.peer_id}
                            size="xl"
                            className="!w-24 !h-24 md:!w-32 md:!h-32 border-4 border-white shadow-xl rounded-full bg-white"
                        />
                    </div>

                    <div className="flex items-center space-x-2 pb-1">
                        {/* Sync Button */}
                        {syncChecked && (
                            <button
                                onClick={handleSync}
                                disabled={syncLoading}
                                className={`px-4 py-2 rounded-full text-sm font-bold shadow-md transition-all active:scale-95 flex items-center gap-2 ${isSynced
                                        ? 'bg-gray-100 text-gray-700 border border-gray-200 hover:bg-red-50 hover:text-red-500 hover:border-red-200'
                                        : 'bg-primary text-white hover:bg-purple-700'
                                    }`}
                            >
                                {syncLoading ? (
                                    <FaSpinner className="text-xs animate-spin" />
                                ) : (
                                    <FaArrowsRotate className={`text-xs ${isSynced ? '' : 'animate-spin-slow'}`} />
                                )}
                                <span>{isSynced ? 'Synced' : 'Sync'}</span>
                            </button>
                        )}

                        {/* Message Button */}
                        <Link
                            href={`/messages/${profile.peer_id}`}
                            className="bg-white text-gray-700 border border-gray-200 px-4 py-2 rounded-full text-sm font-bold shadow-sm hover:bg-gray-50 transition-all active:scale-95 flex items-center gap-2"
                        >
                            <FaMessage className="text-xs" /> <span className="hidden md:inline">Message</span>
                        </Link>

                        {/* Share Button */}
                        <button
                            onClick={() => {
                                navigator.clipboard.writeText(window.location.href);
                                showToast('Link copied!', 'success');
                            }}
                            className="w-9 h-9 flex items-center justify-center bg-gray-100 rounded-full hover:bg-gray-200 transition-colors text-foreground border border-gray-200"
                        >
                            <FaShare className="text-sm" />
                        </button>
                    </div>
                </div>
            </div>

            {/* --- PROFILE INFO --- */}
            <div className="px-5 space-y-4">
                <div>
                    <h1 className="text-2xl font-black text-gray-900 tracking-tight">{profile.username || 'Anonymous'}</h1>
                    <div className="flex items-center space-x-2 text-sm text-gray-500 mt-0.5">
                        <span>@{(profile.handle || (profile.username || 'user').toLowerCase().replace(/\s+/g, '_')).replace(/^@/, '')}</span>
                        {profile.peer_id && (
                            <button
                                onClick={() => {
                                    navigator.clipboard.writeText(profile.peer_id || '');
                                    showToast('DID copied', 'success');
                                }}
                                className="flex items-center space-x-1 bg-gray-50 px-1.5 py-0.5 rounded text-xs hover:bg-gray-100 transition-colors"
                            >
                                <span className="opacity-50 font-mono">{profile.peer_id.slice(0, 4)}...{profile.peer_id.slice(-4)}</span>
                                <FaCopy className="text-[10px] opacity-50" />
                            </button>
                        )}
                    </div>
                </div>

                {/* Bio */}
                {profile.bio && (
                    <p className="text-sm text-gray-700 leading-relaxed max-w-2xl">
                        {profile.bio}
                    </p>
                )}

                {/* Tags */}
                <div className="flex flex-wrap gap-2">
                    {(profile.tags || []).map((tag, i) => (
                        <span key={i} className="px-3 py-1 bg-gray-50 text-gray-600 text-xs font-bold rounded-lg border border-gray-100">
                            #{tag}
                        </span>
                    ))}
                    {profile.location && (
                        <span className="px-3 py-1 bg-gray-50 text-gray-600 text-xs font-bold rounded-lg border border-gray-100 flex items-center gap-1">
                            📍 {profile.location}
                        </span>
                    )}
                </div>

                {/* Stats Row */}
                <div className="flex space-x-6 py-2 border-b border-gray-50 md:border-none">
                    <div className="text-center md:text-left cursor-pointer hover:opacity-70">
                        <span className="block font-black text-lg text-gray-900">{myPosts.length}</span>
                        <span className="text-xs text-gray-500 font-bold uppercase tracking-wide">Posts</span>
                    </div>
                    <div className="text-center md:text-left cursor-pointer hover:opacity-70">
                        <span className="block font-black text-lg text-gray-900">{profile.stats?.syncs || 0}</span>
                        <span className="text-xs text-gray-500 font-bold uppercase tracking-wide">Syncs</span>
                    </div>
                    <div className="text-center md:text-left cursor-pointer hover:opacity-70">
                        <span className="block font-black text-lg text-gray-900">{profile.stats?.contacts || 0}</span>
                        <span className="text-xs text-gray-500 font-bold uppercase tracking-wide">Peers</span>
                    </div>
                </div>
            </div>

            {/* --- NAVIGATION TABS --- */}
            <div className="sticky top-0 z-30 bg-white/95 backdrop-blur-sm border-b border-gray-100 mt-4 overflow-x-auto scrollbar-hide">
                <div className="flex min-w-full md:min-w-0 justify-between md:justify-start md:space-x-8 px-5">
                    <TabButton active={activeTab === 'feed'} onClick={() => setActiveTab('feed')} icon={<FaList />} label="Feed" />
                    <TabButton active={activeTab === 'media'} onClick={() => setActiveTab('media')} icon={<FaImages />} label="Media" />
                    <TabButton active={activeTab === 'files'} onClick={() => setActiveTab('files')} icon={<FaFileLines />} label="Files" />
                    <TabButton active={activeTab === 'recommended'} onClick={() => setActiveTab('recommended')} icon={<FaHeart />} label="Recommended" />
                </div>
            </div>

            {/* --- CONTENT AREA --- */}
            <div className="min-h-[50vh] bg-gray-50">

                {/* 1. FEED VIEW */}
                {activeTab === 'feed' && (
                    <div className="md:max-w-xl md:mx-auto py-0 md:py-6 space-y-0 md:space-y-6">
                        {myPosts.map(post => (
                            <div key={post.cid} className="bg-white md:rounded-2xl md:shadow-sm md:border md:border-gray-100 mb-2 md:mb-0">
                                <PostCard
                                    item={post}
                                    interactions={interactions[post.cid]}
                                />
                            </div>
                        ))}
                        {myPosts.length === 0 && (
                            <div className="py-20 text-center text-gray-400">
                                <p>No posts yet.</p>
                            </div>
                        )}
                    </div>
                )}

                {/* 2. MEDIA VIEW */}
                {activeTab === 'media' && (
                    <div className="grid grid-cols-3 gap-0.5 md:gap-4 md:p-4">
                        {mediaPosts.map((post, i) => (
                            <Link key={post.cid} href={`/feed?cid=${post.cid}`} className="aspect-square relative group overflow-hidden bg-gray-200 md:rounded-xl">
                                {post.filename?.toLowerCase().endsWith('.mp4') ? (
                                    <video src={getIPFSUrl(post.cid)} className="w-full h-full object-cover" />
                                ) : (
                                    <img src={getIPFSUrl(post.cid)} alt="Post" className="w-full h-full object-cover transition-transform duration-500 group-hover:scale-110" />
                                )}
                                <div className="absolute inset-0 bg-black/0 group-hover:bg-black/20 transition-colors" />
                            </Link>
                        ))}
                        {mediaPosts.length === 0 && (
                            <div className="col-span-3 py-20 text-center text-gray-400">
                                <div className="w-16 h-16 bg-gray-100 rounded-full flex items-center justify-center mx-auto mb-4">
                                    <FaImages className="text-2xl opacity-20" />
                                </div>
                                <p className="text-sm font-medium">No photos or videos shared yet.</p>
                            </div>
                        )}
                    </div>
                )}

                {/* 3. FILES VIEW */}
                {activeTab === 'files' && (
                    <div className="md:max-w-xl md:mx-auto py-0 md:py-6 space-y-2 p-4">
                        {filePosts.length === 0 ? (
                            <div className="py-20 text-center text-gray-400">
                                <div className="w-16 h-16 bg-gray-100 rounded-full flex items-center justify-center mx-auto mb-4">
                                    <FaFileLines className="text-2xl opacity-20" />
                                </div>
                                <p className="text-sm font-medium">No files shared yet.</p>
                            </div>
                        ) : (
                            filePosts.map(post => (
                                <div key={post.cid} className="bg-white p-4 rounded-xl border border-gray-100 flex items-center justify-between">
                                    <div className="flex items-center space-x-4">
                                        <div className="w-10 h-10 bg-blue-50 text-blue-500 rounded-lg flex items-center justify-center">
                                            <FaFileLines />
                                        </div>
                                        <div>
                                            <h4 className="font-bold text-gray-900 text-sm">{post.name}</h4>
                                            <FormattedDate date={post.timestamp} className="text-xs text-gray-500" />
                                        </div>
                                    </div>
                                    <Link href={getIPFSUrl(post.cid)} target="_blank" className="px-3 py-1 bg-gray-100 rounded text-xs font-bold hover:bg-gray-200 transition-colors">
                                        Download
                                    </Link>
                                </div>
                            ))
                        )}
                    </div>
                )}

                {/* 4. RECOMMENDED VIEW (What this user recommended/liked) */}
                {activeTab === 'recommended' && (
                    <div className="md:max-w-xl md:mx-auto py-0 md:py-6 space-y-0 md:space-y-6">
                        {recommendedPosts.length > 0 ? (
                            recommendedPosts.map(post => (
                                <div key={post.cid} className="bg-white md:rounded-2xl md:shadow-sm md:border md:border-gray-100 mb-2 md:mb-0">
                                    <PostCard
                                        item={post}
                                        interactions={interactions[post.cid]}
                                    />
                                </div>
                            ))
                        ) : (
                            <div className="py-20 text-center text-gray-400">
                                <div className="w-16 h-16 bg-pink-50 rounded-full flex items-center justify-center mx-auto mb-4">
                                    <FaHeart className="text-2xl text-pink-200" />
                                </div>
                                <p className="text-sm font-medium">No recommendations yet.</p>
                                <p className="text-xs text-gray-300 mt-1">
                                    Posts that {profile.username || 'this user'} recommends will appear here.
                                </p>
                            </div>
                        )}
                    </div>
                )}
            </div>
        </div>
    );
}

function TabButton({ active, onClick, icon, label }: { active: boolean; onClick: () => void; icon: React.ReactNode; label: string }) {
    return (
        <button
            onClick={onClick}
            className={`flex-1 md:flex-none flex items-center justify-center space-x-2 py-4 border-b-2 transition-all min-w-[80px] ${active
                ? 'border-gray-900 text-gray-900'
                : 'border-transparent text-gray-400 hover:text-gray-600'
                }`}
        >
            <span className={`text-lg ${active ? 'scale-110' : ''} transition-transform`}>{icon}</span>
            <span className={`text-xs font-bold uppercase tracking-wide hidden sm:block`}>{label}</span>
        </button>
    );
}
