"use client";

import React, { useState } from 'react';
import { FaArrowUp, FaArrowDown, FaRegComment, FaShare } from 'react-icons/fa6';
import { toggleLike, toggleDislike } from '../lib/api';

interface EngagementBarProps {
    cid: string;
    initialRecommended: boolean;
    initialNotRecommended: boolean;
    commentsCount: number;
    onCommentClick: () => void;
}

const EngagementBar = ({
    cid,
    initialRecommended,
    initialNotRecommended,
    commentsCount,
    onCommentClick,
    onInteractionUpdate,
    likes_count = 0,
    dislikes_count = 0
}: EngagementBarProps & { likes_count?: number; dislikes_count?: number; onInteractionUpdate?: (updates: any) => void }) => {
    const [recommended, setRecommended] = useState(initialRecommended);
    const [notRecommended, setNotRecommended] = useState(initialNotRecommended);
    const [likesCount, setLikesCount] = useState(likes_count);
    const [dislikesCount, setDislikesCount] = useState(dislikes_count);

    const handleLike = async () => {
        // Optimistic update
        const newRecommended = !recommended;
        setRecommended(newRecommended);
        setLikesCount(prev => newRecommended ? prev + 1 : prev - 1);

        if (newRecommended && notRecommended) {
            setNotRecommended(false);
            setDislikesCount(prev => prev - 1);
        }

        try {
            await toggleLike(cid);

            // Notify parent
            onInteractionUpdate?.({
                recommended: newRecommended,
                not_recommended: newRecommended ? false : notRecommended,
                likes_count: newRecommended ? likesCount + 1 : likesCount - 1,
                dislikes_count: newRecommended && notRecommended ? dislikesCount - 1 : dislikesCount
            });
        } catch (error) {
            // Revert on error
            console.error('Failed to like', error);
            setRecommended(!newRecommended);
            setLikesCount(prev => !newRecommended ? prev + 1 : prev - 1);
        }
    };

    const handleDislike = async () => {
        const newNotRecommended = !notRecommended;
        setNotRecommended(newNotRecommended);
        setDislikesCount(prev => newNotRecommended ? prev + 1 : prev - 1);

        if (newNotRecommended && recommended) {
            setRecommended(false);
            setLikesCount(prev => prev - 1);
        }

        try {
            await toggleDislike(cid);

            // Notify parent
            onInteractionUpdate?.({
                recommended: newNotRecommended ? false : recommended,
                not_recommended: newNotRecommended,
                likes_count: newNotRecommended && recommended ? likesCount - 1 : likesCount,
                dislikes_count: newNotRecommended ? dislikesCount + 1 : dislikesCount - 1
            });
        } catch (error) {
            console.error('Failed to dislike', error);
            setNotRecommended(!newNotRecommended);
            setDislikesCount(prev => !newNotRecommended ? prev + 1 : prev - 1);
        }
    };

    return (
        <div className="flex items-center justify-between py-2 px-4 mt-1">
            <div className="flex items-center space-x-4">
                {/* Like / Vote Actions */}
                <div className="flex items-center space-x-1">
                    <button
                        onClick={handleLike}
                        className={`flex items-center justify-center w-8 h-8 rounded-full transition-transform active:scale-95 ${recommended
                            ? 'text-green-600'
                            : 'text-gray-800 hover:text-green-600'
                            }`}
                    >
                        <FaArrowUp className="text-xl" />
                    </button>
                    {(likesCount > 0 || dislikesCount > 0) && (
                        <span className="text-sm font-bold min-w-[1ch] text-center">{likesCount - dislikesCount}</span>
                    )}
                    <button
                        onClick={handleDislike}
                        className={`flex items-center justify-center w-8 h-8 rounded-full transition-transform active:scale-95 ${notRecommended
                            ? 'text-red-500'
                            : 'text-gray-800 hover:text-red-500'
                            }`}
                    >
                        <FaArrowDown className="text-xl" />
                    </button>
                </div>

                {/* Comment Action */}
                <button
                    onClick={onCommentClick}
                    className="flex items-center justify-center w-8 h-8 rounded-full text-gray-800 hover:text-primary transition-all active:scale-95"
                >
                    <FaRegComment className="text-xl" />
                </button>

                {/* Share Action */}
                <button
                    onClick={() => {
                        const shareUrl = `${window.location.origin}/feed?cid=${cid}`;
                        navigator.clipboard.writeText(shareUrl)
                            .then(() => alert('Link copied!'))
                            .catch(() => prompt('Copy link:', shareUrl));
                    }}
                    className="flex items-center justify-center w-8 h-8 rounded-full text-gray-800 hover:text-primary transition-all active:scale-95"
                >
                    <FaShare className="text-xl" />
                </button>
            </div>
        </div>
    );
};

export default EngagementBar;
