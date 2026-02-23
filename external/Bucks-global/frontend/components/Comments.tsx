"use client";

import React, { useState } from 'react';
import { FaTrash, FaUser, FaXmark, FaRegComments, FaArrowUp } from 'react-icons/fa6';
import { addComment, deleteComment } from '../lib/api';

interface CommentsProps {
    cid: string;
    comments: string[];
    onCommentAdded: () => void;
}

const Comments = ({ cid, comments, onCommentAdded, onClose }: CommentsProps & { onClose?: () => void }) => {
    const [newComment, setNewComment] = useState('');
    const [isSubmitting, setIsSubmitting] = useState(false);

    const handleSubmit = async (e: React.FormEvent) => {
        e.preventDefault();
        if (!newComment.trim()) return;

        setIsSubmitting(true);
        try {
            await addComment(cid, newComment);
            setNewComment('');
            onCommentAdded();
        } catch (error) {
            console.error('Failed to add comment', error);
        } finally {
            setIsSubmitting(false);
        }
    };

    const handleDelete = async (index: number) => {
        if (!confirm('Are you sure you want to delete this comment?')) return;
        try {
            await deleteComment(cid, index);
            onCommentAdded();
        } catch (error) {
            console.error('Failed to delete comment', error);
        }
    };

    return (
        <div className="fixed inset-x-0 bottom-0 z-50 md:relative md:inset-auto md:z-auto flex flex-col max-h-[85vh] md:max-h-none bg-white md:bg-gray-50 rounded-t-3xl md:rounded-none shadow-2xl md:shadow-none border-t border-gray-100 animate-in slide-in-from-bottom duration-300">
            {/* Mobile Header handle */}
            <div className="md:hidden flex items-center justify-between p-4 border-b border-gray-100">
                <span className="font-bold text-sm">Comments ({comments.length})</span>
                <button onClick={onClose} className="p-2 bg-gray-100 rounded-full">
                    <FaXmark className="text-gray-500" />
                </button>
            </div>

            <div className="flex-1 overflow-y-auto p-4 space-y-4 min-h-[50vh] md:min-h-0">
                {comments.length === 0 ? (
                    <div className="flex flex-col items-center justify-center py-10 text-center opacity-50">
                        <FaRegComments className="text-4xl mb-2" />
                        <p className="text-sm">No comments yet.</p>
                        <p className="text-xs">Start the conversation!</p>
                    </div>
                ) : (
                    comments.map((comment, index) => (
                        <div key={index} className="flex space-x-3 group animate-in fade-in slide-in-from-bottom-2">
                            <div className="w-8 h-8 rounded-full bg-gradient-to-br from-gray-100 to-gray-200 flex items-center justify-center flex-shrink-0 border border-white shadow-sm">
                                <FaUser className="text-gray-400 text-xs" />
                            </div>
                            <div className="flex-1 bg-gray-50 p-3 rounded-2xl rounded-tl-none text-sm group-hover:bg-white transition-colors border border-transparent group-hover:border-gray-100">
                                <div className="flex justify-between items-start mb-1">
                                    <span className="font-bold text-xs text-foreground">User</span>
                                    <button
                                        onClick={() => handleDelete(index)}
                                        className="text-gray-300 hover:text-red-500 transition-colors opacity-0 group-hover:opacity-100"
                                    >
                                        <FaTrash className="text-xs" />
                                    </button>
                                </div>
                                <p className="text-gray-700 leading-relaxed">{comment}</p>
                            </div>
                        </div>
                    ))
                )}
            </div>

            <div className="p-4 border-t border-gray-100 bg-white pb-safe">
                <form onSubmit={handleSubmit} className="flex items-center space-x-2">
                    <div className="w-8 h-8 rounded-full bg-gray-100 flex-shrink-0" />
                    <input
                        type="text"
                        value={newComment}
                        onChange={(e) => setNewComment(e.target.value)}
                        placeholder="Add a comment..."
                        className="flex-1 bg-gray-50 px-4 py-2.5 rounded-full text-sm focus:outline-none focus:ring-2 focus:ring-primary/20 transition-all"
                        disabled={isSubmitting}
                        autoFocus
                    />
                    <button
                        type="submit"
                        disabled={isSubmitting || !newComment.trim()}
                        className="p-2.5 bg-primary text-white rounded-full hover:scale-105 active:scale-95 disabled:opacity-50 disabled:scale-100 transition-all"
                    >
                        {isSubmitting ? <div className="w-4 h-4 border-2 border-white/30 border-t-white rounded-full animate-spin" /> : <FaArrowUp className="text-sm" />}
                    </button>
                </form>
            </div>

            {/* Backdrop for mobile */}
            <div className="md:hidden fixed inset-0 bg-black/50 z-[-1]" onClick={onClose} />
        </div>
    );
};

export default Comments;
