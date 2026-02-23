"use client";

import React, { useState, useEffect } from 'react';
import { FaArrowsRotate, FaCircleCheck, FaSpinner } from 'react-icons/fa6';

interface SyncButtonProps {
    peerId: string;
    initialSynced?: boolean;
    onSyncChange?: (synced: boolean) => void;
}

export default function SyncButton({ peerId, initialSynced = false, onSyncChange }: SyncButtonProps) {
    const [isSynced, setIsSynced] = useState(initialSynced);
    const [isLoading, setIsLoading] = useState(false);

    const handleSync = async () => {
        setIsLoading(true);
        try {
            const host = typeof window !== 'undefined' ? window.location.hostname : 'localhost';
            const endpoint = isSynced
                ? `http://${host}:8000/api/unfollow/${peerId}`
                : `http://${host}:8000/api/follow/${peerId}`;

            const response = await fetch(endpoint, {
                method: 'POST'
            });

            if (response.ok) {
                const newState = !isSynced;
                setIsSynced(newState);
                onSyncChange?.(newState);
            } else {
                const error = await response.json();
                alert(error.detail || 'Failed to update sync status');
            }
        } catch (error) {
            console.error('Sync error:', error);
            alert('Network error');
        } finally {
            setIsLoading(false);
        }
    };

    return (
        <button
            onClick={handleSync}
            disabled={isLoading}
            className={`flex items-center space-x-2 px-4 py-2 rounded-lg font-medium text-sm transition-all ${isSynced
                ? 'bg-gray-100 text-foreground hover:bg-gray-200'
                : 'bg-primary text-white hover:bg-primary/90 shadow-lg shadow-primary/20'
                } disabled:opacity-50 disabled:cursor-not-allowed`}
        >
            {isLoading ? (
                <>
                    <FaSpinner className="animate-spin" />
                    <span>{isSynced ? 'Unsyncing...' : 'Syncing...'}</span>
                </>
            ) : isSynced ? (
                <>
                    <FaCircleCheck />
                    <span>Synced</span>
                </>
            ) : (
                <>
                    <FaArrowsRotate />
                    <span>Sync</span>
                </>
            )}
        </button>
    );
}
