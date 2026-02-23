"use client";

import React, { useState, useEffect, useRef } from 'react';
import Link from 'next/link';
import { FaUser, FaEllipsis, FaTrash, FaPen, FaXmark, FaCheck, FaShare, FaHeart } from 'react-icons/fa6';
import { LibraryItem, getIPFSUrl, fetchInteractions, Interaction, deletePost, updatePost, api } from '../lib/api';
import EngagementBar from './EngagementBar';
import Comments from './Comments';
import { useToast } from './Toast';
import Avatar from './Avatar';
import { FaFilePdf, FaFileZipper, FaFileLines, FaFileCode } from 'react-icons/fa6';
import FormattedDate from './FormattedDate';

interface PostCardProps {
    item: LibraryItem;
    interactions?: Interaction;
    onPostDeleted?: (cid: string) => void;
    onPostUpdated?: (cid: string, newTitle: string, newDescription: string) => void;
}

const timeAgo = (dateStr: string) => {
    if (!dateStr) return '';
    try {
        const date = new Date(dateStr);
        const now = new Date();
        const seconds = Math.floor((now.getTime() - date.getTime()) / 1000);
        if (seconds < 60) return 'just now';
        const minutes = Math.floor(seconds / 60);
        if (minutes < 60) return `${minutes}m`;
        const hours = Math.floor(minutes / 60);
        if (hours < 24) return `${hours}h`;
        const days = Math.floor(hours / 24);
        if (days < 7) return `${days}d`;
        return date.toLocaleDateString();
    } catch (e) { return ''; }
};

const PostCard = ({ item, interactions: initialInteractions, onPostDeleted, onPostUpdated }: PostCardProps) => {
    const post = item;
    const { showToast } = useToast();
    const [showComments, setShowComments] = useState(false);
    const [showMenu, setShowMenu] = useState(false);
    const [isEditing, setIsEditing] = useState(false);
    const [editTitle, setEditTitle] = useState(item.name);
    const [editDescription, setEditDescription] = useState(item.description);
    const [isDeleting, setIsDeleting] = useState(false);
    const menuRef = useRef<HTMLDivElement>(null);
    const [interactions, setInteractions] = useState(initialInteractions || {
        recommended: false,
        not_recommended: false,
        comments: [] as string[],
        views: 0,
        likes_count: 0,
        dislikes_count: 0
    });

    const isRecommended = interactions.recommended;

    const loadInteractions = async () => {
        try {
            const data = await fetchInteractions(item.cid);
            setInteractions(data);
        } catch (error) {
            console.error('Failed to load interactions', error);
        }
    };

    useEffect(() => {
        loadInteractions();
    }, [item.cid]);

    // Close menu when clicking outside
    useEffect(() => {
        const handleClickOutside = (event: MouseEvent) => {
            if (menuRef.current && !menuRef.current.contains(event.target as Node)) {
                setShowMenu(false);
            }
        };
        document.addEventListener('mousedown', handleClickOutside);
        return () => document.removeEventListener('mousedown', handleClickOutside);
    }, []);

    const handleDelete = async () => {
        if (!confirm('Are you sure you want to delete this post?')) return;
        setIsDeleting(true);
        try {
            await deletePost(item.cid);
            onPostDeleted?.(item.cid);
        } catch (error) {
            console.error('Failed to delete post', error);
            alert('Failed to delete post.');
        } finally {
            setIsDeleting(false);
            setShowMenu(false);
        }
    };

    const handleEdit = () => {
        setIsEditing(true);
        setShowMenu(false);
    };

    const handleSaveEdit = async () => {
        try {
            await updatePost(item.cid, editTitle, editDescription);
            onPostUpdated?.(item.cid, editTitle, editDescription);
            setIsEditing(false);
        } catch (error) {
            console.error('Failed to update post', error);
            alert('Failed to update post.');
        }
    };

    const handleCancelEdit = () => {
        setEditTitle(item.name);
        setEditDescription(item.description);
        setIsEditing(false);
    };

    // For username synchronization: check if this is the current user
    const [authorName, setAuthorName] = useState(item.author);
    const [authorAvatar, setAuthorAvatar] = useState(item.avatar);

    useEffect(() => {
        const syncName = () => {
            if (typeof window !== 'undefined') {
                const myPeerId = localStorage.getItem('bucks_peer_id');
                // Check if it's me
                if (myPeerId && item.peer_id === myPeerId) {
                    const savedProfile = localStorage.getItem('bucks_user_profile');
                    if (savedProfile) {
                        try {
                            const profile = JSON.parse(savedProfile);
                            if (profile.username && profile.username !== authorName) setAuthorName(profile.username);
                            if (profile.avatar && profile.avatar !== authorAvatar) setAuthorAvatar(profile.avatar);
                        } catch (e) { /* ignore */ }
                    }
                }
            }
        };

        syncName();
        // Poll for local profile updates to keep posts in sync without full refetch
        const interval = setInterval(syncName, 3000);
        return () => clearInterval(interval);
    }, [item.peer_id]);

    const filename = item.filename || '';
    const isImage = filename.match(/\.(jpg|jpeg|png|gif)$/i);
    const isVideo = filename.match(/\.(mp4|webm|mov)$/i);
    const ipfsUrl = getIPFSUrl(item.cid);

    if (isDeleting) {
        return (
            <div className="bg-white border-b border-gray-100 pb-2 mb-2 p-8 text-center animate-fade-in">
                <div className="animate-spin rounded-full h-6 w-6 border-b-2 border-red-500 mx-auto mb-2"></div>
                <p className="text-sm text-secondary">Deleting post...</p>
            </div>
        );
    }

    return (
        <div className="bg-white border-b border-gray-100 pb-2 mb-2 group">
            {/* Social Discovery Header */}
            {item.recommended_by && item.recommended_by.length > 0 && (
                <div className="px-4 py-2 bg-gray-50/50 flex items-center space-x-2 border-b border-gray-50 mb-1">
                    <div className="flex -space-x-2">
                        {item.recommended_by.slice(0, 3).map((name, i) => (
                            <div key={i} className="w-5 h-5 rounded-full bg-primary/10 border-2 border-white flex items-center justify-center text-[8px] font-bold text-primary">
                                {name[0].toUpperCase()}
                            </div>
                        ))}
                    </div>
                    <span className="text-[10px] text-gray-400 font-medium tracking-tight">
                        Recommended by <span className="text-gray-600 font-bold">{item.recommended_by[0]}</span>
                        {item.recommended_by.length > 1 && ` and ${item.recommended_by.length - 1} other${item.recommended_by.length > 2 ? 's' : ''}`}
                    </span>
                </div>
            )}

            <div className="flex items-center justify-between px-4 py-3">
                <div className="flex items-center space-x-3">
                    <Link href={`/profile/${post.peer_id || post.author}`} className="relative">
                        <Avatar src={authorAvatar} seed={post.peer_id || post.author} size="sm" className="ring-2 ring-white" />
                    </Link>
                    <div className="flex flex-col">
                        <div className="flex items-center space-x-2">
                            <Link href={`/profile/${post.peer_id || post.author}`} className="font-bold text-sm text-foreground hover:underline">
                                {authorName || 'User'}
                            </Link>
                            <span className="text-gray-300 text-[10px]">•</span>
                            <FormattedDate date={post.timestamp} relative className="text-secondary text-xs" />
                        </div>
                        {/* Optional location or handle could go here */}
                    </div>
                </div>

                <div className="flex items-center space-x-1">
                    {/* Recommended Badge - Minimalist */}
                    {isRecommended && (
                        <div className="flex items-center text-[10px] font-bold text-primary bg-primary/5 px-2 py-1 rounded-full">
                            <FaHeart className="mr-1" />
                            <span>Rec</span>
                        </div>
                    )}

                    {/* Options Menu */}
                    <div className="relative group" ref={menuRef}>
                        <button
                            onClick={() => setShowMenu(!showMenu)}
                            className="p-2 text-gray-400 hover:text-foreground transition-all active:scale-95"
                        >
                            <FaEllipsis />
                        </button>
                        {showMenu && (
                            <div className="absolute right-0 top-10 bg-white border border-gray-200 rounded-xl shadow-lg py-1 min-w-[160px] z-20 animate-fade-in">
                                <button
                                    onClick={() => {
                                        const shareUrl = `${window.location.origin}/feed?cid=${post.cid}`;
                                        navigator.clipboard.writeText(shareUrl);
                                        showToast('Post link copied to clipboard!', 'success');
                                        setShowMenu(false);
                                    }}
                                    className="flex items-center space-x-3 w-full px-4 py-2.5 text-sm text-foreground hover:bg-gray-50 transition-colors"
                                >
                                    <FaShare className="text-xs text-secondary" />
                                    <span>Share Post</span>
                                </button>
                                <button
                                    onClick={handleDelete}
                                    className="flex items-center space-x-3 w-full px-4 py-2.5 text-sm text-red-600 hover:bg-red-50 transition-colors"
                                >
                                    <FaTrash className="text-xs" />
                                    <span>Delete Post</span>
                                </button>
                            </div>
                        )}
                    </div>
                </div>
            </div>

            {/* Caption / Edit Mode */}
            <div className="px-4 mb-3">
                {isEditing ? (
                    <div className="space-y-2">
                        <input
                            type="text"
                            value={editTitle}
                            onChange={(e) => setEditTitle(e.target.value)}
                            className="w-full px-3 py-2 border border-gray-200 rounded-lg text-sm focus:outline-none focus:border-primary"
                            placeholder="Title"
                        />
                        <textarea
                            value={editDescription}
                            onChange={(e) => setEditDescription(e.target.value)}
                            className="w-full px-3 py-2 border border-gray-200 rounded-lg text-sm focus:outline-none focus:border-primary resize-none"
                            placeholder="Description"
                            rows={2}
                        />
                        <div className="flex space-x-2">
                            <button
                                onClick={handleSaveEdit}
                                className="flex items-center space-x-1 px-3 py-1.5 bg-primary text-white text-xs rounded-lg font-medium hover:bg-purple-700 transition-colors"
                            >
                                <FaCheck />
                                <span>Save</span>
                            </button>
                            <button
                                onClick={handleCancelEdit}
                                className="flex items-center space-x-1 px-3 py-1.5 bg-gray-100 text-foreground text-xs rounded-lg font-medium hover:bg-gray-200 transition-colors"
                            >
                                <FaXmark />
                                <span>Cancel</span>
                            </button>
                        </div>
                    </div>
                ) : (
                    <>
                        <h4 className="font-medium text-sm mb-1">{post.name}</h4>
                        {post.description && (
                            <p className="text-sm text-secondary">{post.description}</p>
                        )}
                    </>
                )}
            </div>

            {/* Media Area */}
            <div className="w-full bg-black min-h-[300px] flex items-center justify-center relative overflow-hidden">


                {/* Content (Hidden if locked, unless overlay handles z-index) */}
                {/* We render content behind the lock so layout is preserved, but maybe hide it for security if it was text?
                    Here it is media, so overlay is fine. */}

                {isImage ? (
                    <img
                        src={ipfsUrl}
                        alt={post.name}
                        className="w-full h-auto max-h-[500px] object-contain"
                        loading="lazy"
                    />
                ) : isVideo ? (
                    <video
                        src={ipfsUrl}
                        controls
                        className="w-full h-auto max-h-[500px]"
                    />
                ) : post.thumbnail_cid ? (
                    <div className="relative group">
                        <img
                            src={getIPFSUrl(post.thumbnail_cid)}
                            alt={post.name}
                            className="w-full h-auto max-h-[500px] object-contain"
                        />
                        <div className="absolute top-4 right-4 bg-red-600 text-white text-[10px] font-bold px-2 py-1 rounded shadow-lg">
                            PDF
                        </div>
                        <a
                            href={ipfsUrl}
                        >
                            <span className="bg-white text-gray-900 px-4 py-2 rounded-full text-xs font-bold shadow-xl">
                                Open Document
                            </span>
                        </a>
                    </div>
                ) : (
                    <div className="p-12 text-center bg-gradient-to-b from-gray-900 to-black w-full border-y border-white/5">
                        <div className="mb-4 inline-flex items-center justify-center w-20 h-20 rounded-3xl bg-white/5 backdrop-blur-xl border border-white/10 shadow-2xl animate-pulse">
                            {(post.filename || '').match(/\.pdf$/i) ? (
                                <FaFilePdf className="text-red-500 text-3xl" />
                            ) : (post.filename || '').match(/\.(zip|rar|7z|tar)$/i) ? (
                                <FaFileZipper className="text-yellow-500 text-3xl" />
                            ) : (post.filename || '').match(/\.(js|py|html|css|json)$/i) ? (
                                <FaFileCode className="text-blue-500 text-3xl" />
                            ) : (
                                <FaFileLines className="text-primary text-3xl" />
                            )}
                        </div>
                        <h4 className="text-white font-bold mb-3 text-sm">{post.filename || 'Unknown File'}</h4>
                        <a
                            href={ipfsUrl}
                        >
                            Download {post.type || 'File'}
                        </a>
                    </div>
                )}
            </div>

            {/* Engagement */}
            <EngagementBar
                cid={post.cid}
                initialRecommended={interactions.recommended}
                initialNotRecommended={interactions.not_recommended}
                commentsCount={interactions.comments.length}
                onCommentClick={() => setShowComments(!showComments)}
                likes_count={interactions.likes_count}
                dislikes_count={interactions.dislikes_count}
                onInteractionUpdate={(newInteractions: any) => {
                    setInteractions(prev => ({ ...prev, ...newInteractions }));
                }}
            />

            {/* Comments */}
            {showComments && (
                <Comments
                    cid={item.cid}
                    comments={interactions.comments}
                    onCommentAdded={loadInteractions}
                />
            )}
        </div>
    );
};

export default PostCard;
