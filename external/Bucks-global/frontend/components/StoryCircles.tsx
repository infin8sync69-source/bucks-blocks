"use client";

import React, { useState, useMemo, useEffect } from 'react';
import { FaPlus, FaXmark, FaChevronLeft, FaChevronRight, FaFileLines, FaDownload } from 'react-icons/fa6';
import Link from 'next/link';
import Avatar from './Avatar';
import { LibraryItem, getIPFSUrl } from '../lib/api';

interface StoryCirclesProps {
    library: LibraryItem[];
}

export default function StoryCircles({ library }: StoryCirclesProps) {
    const [selectedAuthor, setSelectedAuthor] = useState<string | null>(null);
    const [currentIndex, setCurrentIndex] = useState(0);
    const [myPeerId, setMyPeerId] = useState<string | null>(null);

    useEffect(() => {
        if (typeof window !== 'undefined') {
            setMyPeerId(localStorage.getItem('bucks_peer_id'));
        }
    }, []);

    // Get unique authors with their peer_id
    const uniqueAuthors = useMemo(() => {
        const authors = new Map<string, { avatar: string; peerId: string }>();
        library.forEach((item) => {
            if (!authors.has(item.author)) {
                authors.set(item.author, { avatar: item.avatar || '', peerId: item.peer_id || '' });
            }
        });
        return Array.from(authors.entries()).slice(0, 10);
    }, [library]);

    // My author name (to identify "Your Story" among the circles)
    const myAuthorName = useMemo(() => {
        if (!myPeerId) return null;
        const myPost = library.find(item => item.peer_id === myPeerId);
        return myPost?.author || null;
    }, [library, myPeerId]);

    // Get ALL stories for selected author (newest first)
    const authorStories = useMemo(() => {
        if (!selectedAuthor) return [];
        return library
            .filter(item => item.author === selectedAuthor)
            .sort((a, b) => new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime());
    }, [library, selectedAuthor]);

    const handleOpenStory = (author: string) => {
        setSelectedAuthor(author);
        setCurrentIndex(0);
    };

    const handleNext = (e?: React.MouseEvent) => {
        e?.stopPropagation();
        if (currentIndex < authorStories.length - 1) {
            setCurrentIndex(currentIndex + 1);
        } else {
            setSelectedAuthor(null);
        }
    };

    const handlePrev = (e?: React.MouseEvent) => {
        e?.stopPropagation();
        if (currentIndex > 0) {
            setCurrentIndex(currentIndex - 1);
        }
    };

    const isMediaFile = (filename: string) => {
        return filename?.toLowerCase().match(/\.(jpg|jpeg|png|gif|webp|mp4|webm|ogg)$/i);
    };

    const isVideoFile = (filename: string) => {
        return filename?.toLowerCase().match(/\.(mp4|webm|ogg)$/i);
    };

    // Render story content based on file type
    const renderStoryContent = (story: LibraryItem) => {
        if (isVideoFile(story.filename)) {
            return (
                <div className="w-full h-full flex items-center justify-center">
                    <video
                        src={getIPFSUrl(story.cid)}
                        className="max-w-full max-h-full rounded-2xl shadow-2xl border border-white/10"
                        autoPlay
                        loop
                        playsInline
                        onClick={(e) => e.stopPropagation()}
                    />
                </div>
            );
        }

        if (isMediaFile(story.filename)) {
            return (
                <img
                    src={getIPFSUrl(story.cid)}
                    alt={story.name}
                    className="max-w-full max-h-full object-contain rounded-2xl shadow-2xl border border-white/10 select-none pointer-events-none"
                />
            );
        }

        // Non-media file: styled card
        return (
            <div className="bg-white/10 backdrop-blur-xl border border-white/20 rounded-3xl p-8 max-w-sm w-full text-center" onClick={(e) => e.stopPropagation()}>
                <div className="w-20 h-20 mx-auto mb-6 bg-white/10 rounded-2xl flex items-center justify-center">
                    <FaFileLines className="text-3xl text-white/80" />
                </div>
                <h3 className="text-white font-bold text-lg mb-2">{story.name}</h3>
                <p className="text-white/50 text-xs font-mono mb-1 truncate">{story.filename || 'Unknown file'}</p>
                {story.description && (
                    <p className="text-white/60 text-sm mt-3 line-clamp-3">{story.description}</p>
                )}
                <a
                    href={getIPFSUrl(story.cid)}
                    target="_blank"
                    rel="noopener noreferrer"
                    className="inline-flex items-center space-x-2 mt-6 px-5 py-2.5 bg-white/20 hover:bg-white/30 text-white rounded-full text-sm font-bold transition-colors"
                >
                    <FaDownload className="text-xs" />
                    <span>Open File</span>
                </a>
            </div>
        );
    };

    return (
        <div className="w-full py-4 px-4 bg-background border-b border-gray-100 z-40 overflow-hidden">
            <div className="flex space-x-4 overflow-x-auto pb-4 pt-2 px-4 scrollbar-hide">
                {/* Add Story / Your Story Button - Always First */}
                {myAuthorName && library.some(item => item.peer_id === myPeerId) ? (
                    <button
                        onClick={() => handleOpenStory(myAuthorName)}
                        className="flex flex-col items-center space-y-1 cursor-pointer group focus:outline-none"
                    >
                        <div className="w-16 h-16 rounded-full p-[2px] bg-gradient-to-tr from-gray-300 via-gray-400 to-gray-500 relative">
                            <div className="w-full h-full rounded-full p-[2px] bg-white overflow-hidden">
                                <Avatar seed={myPeerId || "me"} size="lg" className="w-full h-full" />
                            </div>
                            <Link
                                href="/create?type=story"
                                className="absolute bottom-0 right-0 bg-primary text-white w-5 h-5 rounded-full flex items-center justify-center border-2 border-white text-[10px] shadow-sm hover:scale-110 transition-transform z-10"
                                onClick={(e) => e.stopPropagation()}
                            >
                                <FaPlus />
                            </Link>
                        </div>
                        <span className="text-xs font-medium text-gray-500">Your Story</span>
                    </button>
                ) : (
                    <Link href="/create?type=story" className="flex flex-col items-center space-y-1 cursor-pointer group">
                        <div className="w-16 h-16 rounded-full p-[2px] relative">
                            <div className="w-full h-full rounded-full overflow-hidden border-2 border-dashed border-gray-300 group-hover:border-primary transition-colors flex items-center justify-center bg-gray-50">
                                <Avatar seed="me" size="lg" className="opacity-50" />
                            </div>
                            <div className="absolute bottom-0 right-0 bg-primary text-white w-5 h-5 rounded-full flex items-center justify-center border-2 border-white text-[10px] shadow-sm">
                                <FaPlus />
                            </div>
                        </div>
                        <span className="text-xs font-medium text-gray-500">Your Story</span>
                    </Link>
                )}

                {/* Other Authors' Stories (exclude self) */}
                {uniqueAuthors
                    .filter(([author]) => author !== myAuthorName)
                    .map(([author, { avatar }], i) => (
                        <button
                            key={`${author}-${i}`}
                            onClick={() => handleOpenStory(author)}
                            className="flex flex-col items-center space-y-1.5 shrink-0 group focus:outline-none"
                        >
                            <div className={`w-16 h-16 rounded-full p-[2px] ${i < 3
                                ? 'bg-gradient-to-tr from-yellow-400 via-red-500 to-purple-500'
                                : 'bg-gray-200'
                                }`}>
                                <div className="w-full h-full rounded-full p-[2px] bg-white">
                                    <Avatar
                                        src={avatar}
                                        seed={author}
                                        size="xl"
                                        className="w-full h-full"
                                    />
                                </div>
                            </div>
                            <span className="text-xs font-medium text-gray-600 w-16 truncate text-center">{author}</span>
                        </button>
                    ))}
            </div>

            {/* Full-screen Story Viewer Overlay */}
            {selectedAuthor && authorStories.length > 0 && (
                <div
                    className="fixed inset-0 bg-black z-[9999] flex flex-col items-center justify-center overflow-hidden animate-in fade-in duration-300 backdrop-blur-xl"
                    onClick={() => setSelectedAuthor(null)}
                >
                    {/* Background blur layer — only for media */}
                    {isMediaFile(authorStories[currentIndex].filename) && (
                        <div className="absolute inset-0 z-0 opacity-40">
                            <img
                                src={getIPFSUrl(authorStories[currentIndex].cid)}
                                alt=""
                                className="w-full h-full object-cover blur-3xl scale-110"
                            />
                        </div>
                    )}

                    {/* Progress Bars */}
                    <div className="absolute top-6 left-4 right-4 flex space-x-1.5 z-[110]">
                        {authorStories.map((_, i) => (
                            <div key={i} className="flex-1 h-1 bg-white/20 overflow-hidden rounded-full">
                                <div
                                    className={`h-full bg-white transition-all duration-[5000ms] linear ${i < currentIndex ? 'w-full' : i === currentIndex ? 'w-full origin-left' : 'w-0'}`}
                                />
                            </div>
                        ))}
                    </div>

                    {/* Header Info */}
                    <div className="absolute top-10 left-4 right-4 flex items-center justify-between z-[110]">
                        <div className="flex items-center space-x-3">
                            <Avatar src={authorStories[currentIndex].avatar} seed={selectedAuthor} size="sm" className="border border-white/30" />
                            <div>
                                <p className="text-white text-sm font-bold shadow-sm">{selectedAuthor}</p>
                                <p className="text-white/60 text-[10px] uppercase font-mono tracking-wider">{authorStories[currentIndex].timestamp?.split(' ')[0]}</p>
                            </div>
                        </div>
                        <button
                            className="w-10 h-10 rounded-full bg-black/20 text-white flex items-center justify-center backdrop-blur-md hover:bg-black/40 transition-colors"
                            onClick={() => setSelectedAuthor(null)}
                        >
                            <FaXmark className="text-xl" />
                        </button>
                    </div>

                    {/* Navigation Regions (Invisible touch targets) */}
                    <div className="absolute inset-0 flex z-[105]">
                        <div className="w-1/3 h-full cursor-w-resize" onClick={handlePrev}></div>
                        <div className="w-1/3 h-full" onClick={() => setSelectedAuthor(null)}></div>
                        <div className="w-1/3 h-full cursor-e-resize" onClick={handleNext}></div>
                    </div>

                    {/* Navigation Buttons (Desktop) */}
                    <button
                        onClick={handlePrev}
                        className={`hidden md:flex absolute left-6 top-1/2 -translate-y-1/2 w-12 h-12 rounded-full bg-white/10 text-white items-center justify-center backdrop-blur-md z-[110] hover:bg-white/20 transition-all ${currentIndex === 0 ? 'opacity-0 pointer-events-none' : ''}`}
                    >
                        <FaChevronLeft className="text-xl" />
                    </button>
                    <button
                        onClick={handleNext}
                        className="hidden md:flex absolute right-6 top-1/2 -translate-y-1/2 w-12 h-12 rounded-full bg-white/10 text-white items-center justify-center backdrop-blur-md z-[110] hover:bg-white/20 transition-all"
                    >
                        <FaChevronRight className="text-xl" />
                    </button>

                    {/* Content Display */}
                    <div className="relative z-10 w-full max-w-lg h-[75vh] flex items-center justify-center px-4">
                        {renderStoryContent(authorStories[currentIndex])}

                        {/* Title Overlay */}
                        <div className="absolute bottom-6 left-6 right-6 p-4 bg-gradient-to-t from-black/60 to-transparent rounded-b-2xl">
                            <h4 className="text-white font-bold text-lg">{authorStories[currentIndex].name}</h4>
                            <p className="text-white/70 text-xs mt-1 line-clamp-2">{authorStories[currentIndex].description}</p>
                        </div>
                    </div>
                </div>
            )}
        </div>
    );
}
