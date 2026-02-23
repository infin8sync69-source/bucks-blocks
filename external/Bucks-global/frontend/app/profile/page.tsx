"use client";

import React, { useEffect, useState } from 'react';
import Link from 'next/link';
import { useRouter } from 'next/navigation';
import {
    FaUser, FaShare, FaPen, FaEllipsisVertical,
    FaTableCells, FaList, FaUserGroup, FaShieldHalved,
    FaCopy, FaCheck, FaImages, FaFileLines, FaArrowsRotate,
    FaHeart
} from 'react-icons/fa6';
import { fetchProfile, fetchLibrary, UserProfile, LibraryItem, fetchAllInteractions, Interaction, getIPFSUrl, fetchUserLikes } from '@/lib/api';
import PostCard from '@/components/PostCard';
import Avatar from '@/components/Avatar';
import { useToast } from '@/components/Toast';
import FormattedDate from '@/components/FormattedDate';

type Tab = 'feed' | 'media' | 'files' | 'recommended';

export default function Account() {
    const router = useRouter();
    const { showToast } = useToast();
    const [profile, setProfile] = useState<UserProfile | null>(null);
    const [activeTab, setActiveTab] = useState<Tab>('feed');
    const [library, setLibrary] = useState<LibraryItem[]>([]);
    const [interactions, setInteractions] = useState<Record<string, Interaction>>({});
    const [userLikes, setUserLikes] = useState<string[]>([]);

    useEffect(() => {
        // Parallel data fetching
        const loadData = async () => {
            try {
                const [prof, lib, interact] = await Promise.all([
                    fetchProfile(),
                    fetchLibrary(),
                    fetchAllInteractions()
                ]);

                setProfile(prof);
                setLibrary(lib);
                setInteractions(interact);

                if (prof.peer_id) {
                    fetchUserLikes(prof.peer_id).then(setUserLikes).catch(console.error);
                }
            } catch (e) {
                console.error("Failed to load profile data", e);
            }
        };
        loadData();
    }, []);

    if (!profile) return null;

    // --- Content Filtering Logic ---

    // 1. All posts authored by this user
    const myPosts = library.filter(p => p.peer_id === profile.peer_id); // Ensure strictly own posts

    // 2. Media posts (Images/Videos)
    const mediaPosts = myPosts.filter(p =>
        p.filename && (p.filename.toLowerCase().match(/\.(jpg|jpeg|png|gif|webp|mp4|mov|webm)$/i))
    );

    // 3. File posts (Non-media)
    const filePosts = myPosts.filter(p => !mediaPosts.includes(p));

    // 4. Recommended posts (Liked by user)
    // We look at ALL library posts, and check if their CID is in userLikes list
    const recommendedPosts = library.filter(p => userLikes.includes(p.cid));

    return (
        <div className="bg-white min-h-screen pb-20 md:pb-0">
            {/* --- HEADER SECTION --- */}
            <div className="relative mb-16">
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
                        <button
                            onClick={() => router.push('/settings')}
                            className="bg-gray-100 hover:bg-gray-200 text-foreground px-4 py-2 rounded-full text-sm font-bold transition-all active:scale-95 flex items-center gap-2 border border-gray-200"
                        >
                            <FaPen className="text-xs" /> <span className="hidden md:inline">Edit Profile</span>
                        </button>
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

                {/* 1. FEED VIEW (All User Posts) */}
                {activeTab === 'feed' && (
                    <div className="md:max-w-xl md:mx-auto py-0 md:py-6 space-y-0 md:space-y-6">
                        {myPosts.map(post => (
                            <div key={post.cid} className="bg-white md:rounded-2xl md:shadow-sm md:border md:border-gray-100 mb-2 md:mb-0">
                                <PostCard
                                    item={post}
                                    interactions={interactions[post.cid]}
                                    onPostDeleted={(cid) => setLibrary(p => p.filter(x => x.cid !== cid))}
                                    onPostUpdated={(cid, t, d) => setLibrary(p => p.map(x => x.cid === cid ? { ...x, name: t, description: d } : x))}
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

                {/* 2. MEDIA VIEW (Grid of Images/Videos) */}
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

                {/* 3. FILES VIEW (Non-media files) */}
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

                {/* 4. RECOMMENDED VIEW (Liked items) */}
                {activeTab === 'recommended' && (
                    <div className="md:max-w-xl md:mx-auto py-0 md:py-6 space-y-0 md:space-y-6">
                        {recommendedPosts.map(post => (
                            <div key={post.cid} className="bg-white md:rounded-2xl md:shadow-sm md:border md:border-gray-100 mb-2 md:mb-0">
                                <PostCard
                                    item={post}
                                    interactions={interactions[post.cid]}
                                    onPostDeleted={(cid) => setLibrary(p => p.filter(x => x.cid !== cid))}
                                    onPostUpdated={(cid, t, d) => setLibrary(p => p.map(x => x.cid === cid ? { ...x, name: t, description: d } : x))}
                                />
                            </div>
                        ))}
                        {recommendedPosts.length === 0 && (
                            <div className="py-20 text-center text-gray-400">
                                <div className="w-16 h-16 bg-gray-100 rounded-full flex items-center justify-center mx-auto mb-4">
                                    <FaHeart className="text-2xl opacity-20" />
                                </div>
                                <p className="text-sm font-medium">No recommended posts yet.</p>
                                <p className="text-xs text-gray-400 mt-1">Like posts to see them here.</p>
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
