"use client";

import React, { useState, useEffect } from 'react';
import { useRouter } from 'next/navigation';
import { FaUserPlus, FaCheck, FaTimes, FaWifi, FaChevronLeft, FaSpinner } from 'react-icons/fa';
import { fetchConnections, acceptConnectionRequest, discoverPeers, UserProfile, fetchNotifications, markNotificationsRead, Notification } from '@/lib/api';
import { useToast } from '@/components/Toast';
import Avatar from '@/components/Avatar';

const NotificationsPage = () => {
    const router = useRouter();
    const { showToast } = useToast();
    const [connections, setConnections] = useState<{ sent: string[], received: string[], contacts: string[] }>({ sent: [], received: [], contacts: [] });
    const [discovered, setDiscovered] = useState<any[]>([]);
    const [notifications, setNotifications] = useState<Notification[]>([]);
    const [loading, setLoading] = useState(true);
    const [actionLoading, setActionLoading] = useState<string | null>(null);

    useEffect(() => {
        loadData();
    }, []);

    const loadData = async () => {
        try {
            const [connData, discData, notifData] = await Promise.all([
                fetchConnections(),
                discoverPeers(),
                fetchNotifications()
            ]);
            setConnections(connData);
            setDiscovered(discData.peers || []);
            setNotifications(notifData);
            setLoading(false);
        } catch (err) {
            console.error('Failed to load notifications:', err);
            showToast('Failed to load notifications', 'error');
            setLoading(false);
        }
    };

    const handleAccept = async (peerId: string) => {
        setActionLoading(peerId);
        try {
            await acceptConnectionRequest(peerId);
            showToast('Connection accepted!', 'success');
            await loadData();
        } catch (err) {
            showToast('Failed to accept connection', 'error');
        } finally {
            setActionLoading(null);
        }
    };

    const handleNotificationClick = async (notification: Notification) => {
        if (!notification.is_read) {
            await markNotificationsRead(notification.id);
        }
        if (notification.link) {
            router.push(notification.link);
        }
    };

    const markAllRead = async () => {
        await markNotificationsRead();
        loadData();
        showToast('All notifications marked as read', 'success');
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
            <header className="sticky top-0 z-50 bg-white/80 backdrop-blur-md border-b border-gray-100 px-4 py-4 mb-4">
                <div className="flex items-center justify-between">
                    <div className="flex items-center space-x-4">
                        <button onClick={() => router.back()} className="p-2 hover:bg-gray-100 rounded-full transition-colors">
                            <FaChevronLeft className="text-gray-600" />
                        </button>
                        <h1 className="text-xl font-black text-gray-900 tracking-tight">Swarm Alerts</h1>
                    </div>
                    {notifications.some(n => !n.is_read) && (
                        <button onClick={markAllRead} className="text-xs font-bold text-primary hover:text-primary/80">
                            Mark all read
                        </button>
                    )}
                </div>
            </header>

            <div className="max-w-xl mx-auto px-4 space-y-8 mt-2">

                {/* Real Notifications */}
                <section>
                    <h2 className="text-[10px] font-black text-gray-400 uppercase tracking-[0.2em] mb-4">Recent Activity</h2>
                    {notifications.length === 0 ? (
                        <div className="bg-gray-50 rounded-2xl p-6 text-center border border-dashed border-gray-200 mb-6">
                            <p className="text-gray-400 text-sm font-medium">No new notifications.</p>
                        </div>
                    ) : (
                        <div className="space-y-3 mb-8">
                            {notifications.map(notif => (
                                <div
                                    key={notif.id}
                                    onClick={() => handleNotificationClick(notif)}
                                    className={`relative p-4 rounded-2xl border transition-all cursor-pointer ${notif.is_read ? 'bg-white border-gray-100' : 'bg-blue-50/50 border-blue-100'}`}
                                >
                                    {!notif.is_read && (
                                        <div className="absolute top-4 right-4 w-2 h-2 bg-blue-500 rounded-full" />
                                    )}
                                    <div className="flex items-start space-x-3">
                                        <div className={`p-2 rounded-full ${notif.type === 'message' ? 'bg-green-100 text-green-600' : 'bg-gray-100 text-gray-500'}`}>
                                            {notif.type === 'message' ? <FaCheck /> : <FaUserPlus />}
                                        </div>
                                        <div>
                                            <h3 className={`text-sm ${notif.is_read ? 'font-semibold text-gray-900' : 'font-bold text-gray-900'}`}>{notif.title}</h3>
                                            <p className="text-xs text-gray-500 mt-0.5 line-clamp-2">{notif.message}</p>
                                            <p className="text-[10px] text-gray-400 mt-2 font-mono">{new Date(notif.timestamp).toLocaleTimeString()}</p>
                                        </div>
                                    </div>
                                </div>
                            ))}
                        </div>
                    )}
                </section>

                {/* Pending Requests */}
                <section>
                    <h2 className="text-[10px] font-black text-gray-400 uppercase tracking-[0.2em] mb-4">Connection Requests</h2>
                    {connections.received.length === 0 ? (
                        <div className="bg-gray-50 rounded-2xl p-8 text-center border border-dashed border-gray-200">
                            <p className="text-gray-400 text-sm font-medium">No pending requests.</p>
                        </div>
                    ) : (
                        <div className="space-y-3">
                            {connections.received.map(peerId => (
                                <div key={peerId} className="bg-white border border-gray-100 rounded-2xl p-4 shadow-sm flex items-center justify-between">
                                    <div className="flex items-center space-x-4">
                                        <Avatar seed={peerId} size="md" />
                                        <div>
                                            <p className="font-bold text-gray-900 text-sm truncate max-w-[150px]">{peerId}</p>
                                            <p className="text-[10px] text-gray-400 font-bold uppercase tracking-wider">Wants to Sync</p>
                                        </div>
                                    </div>
                                    <div className="flex space-x-2">
                                        <button
                                            onClick={(e) => { e.stopPropagation(); handleAccept(peerId); }}
                                            disabled={!!actionLoading}
                                            className="p-3 bg-primary text-white rounded-xl shadow-lg shadow-primary/20 hover:scale-105 active:scale-95 transition-all"
                                        >
                                            {actionLoading === peerId ? <FaSpinner className="animate-spin" /> : <FaCheck />}
                                        </button>
                                        <button className="p-3 bg-gray-100 text-gray-400 rounded-xl hover:bg-gray-200 transition-all">
                                            <FaTimes />
                                        </button>
                                    </div>
                                </div>
                            ))}
                        </div>
                    )}
                </section>

                {/* Local Peer Discovery */}
                <section>
                    <div className="flex items-center justify-between mb-4">
                        <h2 className="text-[10px] font-black text-gray-400 uppercase tracking-[0.2em]">Nearby on WiFi</h2>
                        <div className="flex items-center space-x-1.5 px-2.5 py-1 bg-blue-50 border border-blue-100 rounded-full">
                            <div className="w-1.5 h-1.5 bg-blue-500 rounded-full animate-pulse" />
                            <span className="text-[9px] font-black text-blue-500 uppercase">Live Discover</span>
                        </div>
                    </div>

                    {discovered.length === 0 ? (
                        <div className="bg-gray-50 rounded-2xl p-8 text-center border border-dashed border-gray-200">
                            <FaWifi className="text-3xl text-gray-200 mx-auto mb-3" />
                            <p className="text-gray-400 text-sm font-medium">Scanning local network...</p>
                        </div>
                    ) : (
                        <div className="grid grid-cols-1 gap-3">
                            {discovered.map(peer => (
                                <div key={peer.peer_id} className="group bg-white border border-gray-100 rounded-2xl p-4 hover:border-primary/30 transition-all flex items-center justify-between">
                                    <div className="flex items-center space-x-4">
                                        <Avatar seed={peer.peer_id} size="md" className="group-hover:scale-110 transition-transform" />
                                        <div>
                                            <p className="font-bold text-gray-900 text-sm truncate max-w-[150px]">{peer.peer_id}</p>
                                            <div className="flex items-center space-x-2">
                                                <span className="text-[10px] text-gray-400 font-medium">{peer.is_local ? 'Local WiFi' : 'External Node'}</span>
                                            </div>
                                        </div>
                                    </div>
                                    <button className="flex items-center space-x-2 px-4 py-2.5 bg-gray-50 text-primary rounded-xl font-bold text-xs hover:bg-primary hover:text-white transition-all">
                                        <FaUserPlus />
                                        <span>Follow</span>
                                    </button>
                                </div>
                            ))}
                        </div>
                    )}
                </section>
            </div>
        </div>
    );
};

export default NotificationsPage;
