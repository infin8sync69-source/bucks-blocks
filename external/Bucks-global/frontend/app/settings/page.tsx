'use client';

import React, { useState, useEffect, useRef } from 'react';
import { useRouter } from 'next/navigation';
import {
    FaUser, FaShieldAlt, FaNetworkWired, FaSignOutAlt,
    FaCamera, FaImage, FaCheck, FaTimes, FaSpinner, FaChevronRight
} from 'react-icons/fa';
import { fetchProfile, updateProfile, uploadFile, logout, UserProfile, getIPFSUrl } from '@/lib/api';
import { useToast } from '@/components/Toast';
import Avatar from '@/components/Avatar';
import InviteQR from '@/components/InviteQR';

const SettingsPage = () => {
    const router = useRouter();
    const { showToast } = useToast();
    const [profile, setProfile] = useState<UserProfile | null>(null);
    const [loading, setLoading] = useState(true);
    const [saving, setSaving] = useState(false);
    const [activeTab, setActiveTab] = useState<'profile' | 'security' | 'network'>('profile');

    const [isOnboarding, setIsOnboarding] = useState(false);

    // Form states
    const [username, setUsername] = useState('');
    const [handle, setHandle] = useState('');
    const [bio, setBio] = useState('');
    const [location, setLocation] = useState('');

    // Refs for file inputs
    const avatarInputRef = useRef<HTMLInputElement>(null);
    const bannerInputRef = useRef<HTMLInputElement>(null);

    useEffect(() => {
        const params = new URLSearchParams(window.location.search);
        if (params.get('onboarding') === 'true') {
            setIsOnboarding(true);
        }
        loadProfile();
    }, []);

    const loadProfile = async () => {
        try {
            const data = await fetchProfile();
            setProfile(data);
            setUsername(data.username || '');
            setHandle(data.handle || '');
            setBio(data.bio || '');
            setLocation(data.location || '');
            setLoading(false);
        } catch (err) {
            console.error('Failed to load profile:', err);
            showToast('Failed to load profile settings', 'error');
            setLoading(false);
        }
    };

    const handleSaveProfile = async () => {
        setSaving(true);
        try {
            await updateProfile({ username, handle, bio, location });
            showToast('Profile updated successfully', 'success');
            await loadProfile();

            if (isOnboarding) {
                router.push('/feed');
            }
        } catch (err) {
            showToast('Failed to update profile', 'error');
        } finally {
            setSaving(false);
        }
    };

    const handleAvatarUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;

        setSaving(true);
        try {
            const formData = new FormData();
            formData.append('file', file);
            formData.append('upload_type', 'avatar');
            const result = await uploadFile(formData);

            if (result.success) {
                await updateProfile({ avatar: result.cid });
                showToast('Profile picture updated', 'success');
                await loadProfile();
            }
        } catch (err) {
            showToast('Failed to upload avatar', 'error');
        } finally {
            setSaving(false);
        }
    };

    const handleBannerUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;

        setSaving(true);
        try {
            const formData = new FormData();
            formData.append('file', file);
            formData.append('upload_type', 'banner');
            const result = await uploadFile(formData);

            if (result.success) {
                await updateProfile({ banner: result.cid });
                showToast('Banner image updated', 'success');
                await loadProfile();
            }
        } catch (err) {
            showToast('Failed to upload banner', 'error');
        } finally {
            setSaving(false);
        }
    };


    if (loading) {
        return (
            <div className="bg-white min-h-screen">
                <div className="flex items-center justify-center h-[60vh]">
                    <FaSpinner className="text-4xl text-primary animate-spin" />
                </div>
            </div>
        );
    }

    return (
        <div className="bg-white min-h-screen pb-20">
            {/* Removed TopBar */}
            <div className="max-w-4xl mx-auto px-4 py-20">
                <h1 className="text-3xl font-bold text-gray-900 mb-8">Settings</h1>

                {isOnboarding && (
                    <div className="mb-8 p-6 bg-primary/5 border border-primary/20 rounded-3xl animate-in slide-in-from-top-4 duration-500">
                        <div className="flex items-center space-x-4">
                            <div className="w-12 h-12 bg-primary rounded-2xl flex items-center justify-center text-white text-xl shadow-lg shadow-primary/30">
                                <FaUser />
                            </div>
                            <div>
                                <h2 className="text-xl font-bold text-primary">Welcome to the Swarm!</h2>
                                <p className="text-secondary text-sm">Let's set up your decentralized profile before you start exploring.</p>
                            </div>
                        </div>
                    </div>
                )}

                <div className="flex flex-col md:flex-row gap-8">
                    {/* Sidebar Nav */}
                    <div className="w-full md:w-64 space-y-2">
                        <button
                            onClick={() => setActiveTab('profile')}
                            className={`w-full flex items-center justify-between px-4 py-3 rounded-xl transition-all ${activeTab === 'profile' ? 'bg-primary text-white shadow-lg shadow-primary/25' : 'text-gray-400 hover:bg-gray-100'
                                }`}
                        >
                            <div className="flex items-center">
                                <FaUser className="mr-3" /> <span>Profile</span>
                            </div>
                            <FaChevronRight className={`text-xs transition-transform ${activeTab === 'profile' ? 'rotate-90' : ''}`} />
                        </button>
                        <button
                            onClick={() => setActiveTab('security')}
                            className={`w-full flex items-center justify-between px-4 py-3 rounded-xl transition-all ${activeTab === 'security' ? 'bg-primary text-white shadow-lg shadow-primary/25' : 'text-gray-400 hover:bg-gray-100'
                                }`}
                        >
                            <div className="flex items-center">
                                <FaShieldAlt className="mr-3" /> <span>Security</span>
                            </div>
                            <FaChevronRight className={`text-xs transition-transform ${activeTab === 'security' ? 'rotate-90' : ''}`} />
                        </button>
                        <button
                            onClick={() => setActiveTab('network')}
                            className={`w-full flex items-center justify-between px-4 py-3 rounded-xl transition-all ${activeTab === 'network' ? 'bg-primary text-white shadow-lg shadow-primary/25' : 'text-gray-400 hover:bg-gray-100'
                                }`}
                        >
                            <div className="flex items-center">
                                <FaNetworkWired className="mr-3" /> <span>Network</span>
                            </div>
                            <FaChevronRight className={`text-xs transition-transform ${activeTab === 'network' ? 'rotate-90' : ''}`} />
                        </button>

                        <div className="pt-4 border-t border-gray-100 mt-4">
                            <button
                                onClick={() => logout()}
                                className="w-full flex items-center px-4 py-3 rounded-xl text-red-500 hover:bg-red-50 transition-all font-semibold"
                            >
                                <FaSignOutAlt className="mr-3" /> <span>Logout</span>
                            </button>
                        </div>
                    </div>

                    {/* Main Content Areas */}
                    <div className="flex-1 bg-white border border-gray-100 rounded-2xl shadow-sm p-6 md:p-8">
                        {activeTab === 'profile' && (
                            <div className="space-y-8 animate-in fade-in slide-in-from-right-4 duration-300">
                                <div>
                                    <h2 className="text-xl font-bold text-gray-900 mb-6">Profile Settings</h2>

                                    {/* Identity Assets */}
                                    <div className="space-y-6 mb-8">
                                        {/* Banner */}
                                        <div className="relative group">
                                            <div className="h-32 w-full rounded-xl bg-gray-100 overflow-hidden border border-gray-100">
                                                {profile?.banner ? (
                                                    <img src={getIPFSUrl(profile.banner)} className="w-full h-full object-cover" alt="Banner" />
                                                ) : (
                                                    <div className="w-full h-full bg-gradient-to-r from-primary/10 to-purple-500/10"></div>
                                                )}
                                            </div>
                                            <button
                                                onClick={() => bannerInputRef.current?.click()}
                                                className="absolute inset-0 flex items-center justify-center bg-black/30 opacity-0 group-hover:opacity-100 transition-opacity rounded-xl"
                                            >
                                                <FaImage className="text-2xl text-white mr-2" />
                                                <span className="text-white font-medium">Change Banner</span>
                                            </button>
                                            <input type="file" ref={bannerInputRef} onChange={handleBannerUpload} className="hidden" accept="image/*" />
                                        </div>

                                        {/* Avatar */}
                                        <div className="flex items-center space-x-6">
                                            <div className="relative group shrink-0">
                                                <Avatar
                                                    src={profile?.avatar}
                                                    seed={profile?.peer_id}
                                                    size="xl"
                                                    className="ring-4 ring-primary/10 transition-transform group-hover:scale-105"
                                                />
                                                <button
                                                    onClick={() => avatarInputRef.current?.click()}
                                                    className="absolute inset-0 flex items-center justify-center bg-black/40 opacity-0 group-hover:opacity-100 transition-all rounded-full"
                                                >
                                                    <FaCamera className="text-white text-xl" />
                                                </button>
                                                <input type="file" ref={avatarInputRef} onChange={handleAvatarUpload} className="hidden" accept="image/*" />
                                            </div>
                                            <div>
                                                <h3 className="text-gray-900 font-black tracking-tight">Identity Avatar</h3>
                                                <p className="text-gray-500 text-xs mt-1 font-medium">Deterministic Identicons secure your identity.</p>
                                            </div>
                                        </div>
                                    </div>

                                    {/* Form Fields */}
                                    <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                                        <div className="space-y-2">
                                            <label className="text-[10px] font-bold text-gray-400 uppercase tracking-wider">Display Name</label>
                                            <input
                                                type="text"
                                                value={username}
                                                onChange={(e) => setUsername(e.target.value)}
                                                className="w-full bg-gray-50 border border-gray-100 rounded-xl px-4 py-2.5 text-gray-900 focus:outline-none focus:ring-2 focus:ring-primary/20 focus:border-primary"
                                                placeholder="Your name"
                                            />
                                        </div>
                                        <div className="space-y-2">
                                            <label className="text-[10px] font-bold text-gray-400 uppercase tracking-wider">Handle (@)</label>
                                            <input
                                                type="text"
                                                value={handle}
                                                onChange={(e) => setHandle(e.target.value)}
                                                className="w-full bg-gray-50 border border-gray-100 rounded-xl px-4 py-2.5 text-gray-900 focus:outline-none focus:ring-2 focus:ring-primary/20 focus:border-primary"
                                                placeholder="unique_handle"
                                            />
                                        </div>
                                        <div className="space-y-2 md:col-span-2">
                                            <label className="text-[10px] font-bold text-gray-400 uppercase tracking-wider">Bio</label>
                                            <textarea
                                                value={bio}
                                                onChange={(e) => setBio(e.target.value)}
                                                rows={3}
                                                className="w-full bg-gray-50 border border-gray-100 rounded-xl px-4 py-2.5 text-gray-900 focus:outline-none focus:ring-2 focus:ring-primary/20 focus:border-primary resize-none"
                                                placeholder="Tell us about yourself..."
                                            />
                                        </div>
                                        <div className="space-y-2 md:col-span-2">
                                            <label className="text-[10px] font-bold text-gray-400 uppercase tracking-wider">Location</label>
                                            <input
                                                type="text"
                                                value={location}
                                                onChange={(e) => setLocation(e.target.value)}
                                                className="w-full bg-gray-50 border border-gray-100 rounded-xl px-4 py-2.5 text-gray-900 focus:outline-none focus:ring-2 focus:ring-primary/20 focus:border-primary"
                                                placeholder="City, Country"
                                            />
                                        </div>
                                    </div>

                                    <div className="mt-8 flex justify-end">
                                        <button
                                            onClick={handleSaveProfile}
                                            disabled={saving}
                                            className="px-8 py-4 bg-primary text-white rounded-full font-bold flex items-center shadow-xl shadow-primary/25 hover:scale-105 active:scale-95 transition-all disabled:opacity-50"
                                        >
                                            {saving ? <FaSpinner className="animate-spin mr-2" /> : <FaCheck className="mr-2" />}
                                            {isOnboarding ? 'Finish Setup' : 'Save Changes'}
                                        </button>
                                    </div>
                                </div>
                            </div>
                        )}

                        {activeTab === 'security' && (
                            <div className="space-y-8 animate-in fade-in slide-in-from-right-4 duration-300">
                                <div>
                                    <h2 className="text-xl font-bold text-gray-900 mb-4">Account Security</h2>
                                    <p className="text-gray-500 mb-8 text-sm">Manage your social recovery guardians and identity backup.</p>

                                    <div className="space-y-4">
                                        <div className="bg-gray-50 border border-gray-100 rounded-xl p-4 flex items-center justify-between">
                                            <div className="flex items-center">
                                                <div className="w-10 h-10 rounded-lg bg-green-100 flex items-center justify-center text-green-600 mr-4 font-bold">7</div>
                                                <div>
                                                    <h3 className="text-gray-900 font-bold">Active Guardians</h3>
                                                    <p className="text-gray-500 text-xs mt-0.5">Threshold: 4 / 7 approvals required</p>
                                                </div>
                                            </div>
                                            <button className="text-primary hover:underline text-sm font-bold">Manage</button>
                                        </div>

                                        <div className="bg-gray-50 border border-gray-100 rounded-xl p-4 flex items-center justify-between">
                                            <div className="flex items-center">
                                                <div className="w-10 h-10 rounded-lg bg-yellow-100 flex items-center justify-center text-yellow-600 mr-4">
                                                    <FaShieldAlt />
                                                </div>
                                                <div>
                                                    <h3 className="text-gray-900 font-bold">DID Identity</h3>
                                                    <p className="text-gray-500 text-xs mt-0.5 font-mono truncate max-w-[150px]">did:key:z6MkpTHR...v4E</p>
                                                </div>
                                            </div>
                                            <button className="text-primary hover:underline text-sm font-bold">Copy</button>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        )}

                        {activeTab === 'network' && (
                            <div className="space-y-8 animate-in fade-in slide-in-from-right-4 duration-300">
                                <div>
                                    <h2 className="text-xl font-bold text-gray-900 mb-4">Network Status</h2>
                                    <p className="text-gray-500 mb-8 text-sm">Current IPFS Node and peer connection details.</p>

                                    <div className="grid grid-cols-1 gap-4">
                                        <div className="bg-gray-50 border border-gray-100 rounded-xl p-4">
                                            <h3 className="text-gray-400 text-[10px] font-bold uppercase tracking-wider mb-2">Peer ID</h3>
                                            <p className="text-gray-900 font-mono text-xs break-all leading-relaxed bg-white p-3 rounded-lg border border-gray-100 select-all">
                                                {profile?.peer_id || 'QmY7Ypbq...Gjrt'}
                                            </p>
                                        </div>
                                        <div className="bg-gray-50 border border-gray-100 rounded-xl p-4 flex items-center justify-between">
                                            <div>
                                                <h3 className="text-gray-900 font-bold">Syncing Status</h3>
                                                <p className="text-gray-500 text-xs mt-0.5">Connected to 12 IPFS peers</p>
                                            </div>
                                            <div className="w-3 h-3 rounded-full bg-green-500 shadow-lg shadow-green-500/50 pulse"></div>
                                        </div>

                                        {/* WiFi Sharing Invite */}
                                        <div className="mt-8">
                                            <h3 className="text-gray-900 font-bold mb-4">Share Your Feed</h3>
                                            <InviteQR
                                                peerId={profile?.peer_id}
                                                did={profile?.did}
                                                className="bg-gray-900"
                                            />
                                        </div>
                                    </div>
                                </div>
                            </div>
                        )}
                    </div>
                </div>
            </div>
            {/* Removed BottomNav */}

            <style jsx>{`
                .pulse {
                    animation: pulse-animation 2s infinite;
                }
                @keyframes pulse-animation {
                    0% { box-shadow: 0 0 0 0px rgba(34, 197, 94, 0.7); }
                    100% { box-shadow: 0 0 0 10px rgba(34, 197, 94, 0); }
                }
            `}</style>
        </div>
    );
};

export default SettingsPage;
