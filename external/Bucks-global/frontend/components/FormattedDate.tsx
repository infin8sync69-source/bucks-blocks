"use client";

import React, { useEffect, useState } from 'react';

const timeAgo = (dateInput: string | number | Date) => {
    if (!dateInput) return '';
    try {
        const date = new Date(dateInput);
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

interface FormattedDateProps {
    date: string | number | Date;
    relative?: boolean;
    className?: string;
}

export default function FormattedDate({ date, relative, className }: FormattedDateProps) {
    const [mounted, setMounted] = useState(false);

    useEffect(() => {
        setMounted(true);
    }, []);

    // Return placeholder during server render to avoid mismatch
    if (!mounted) {
        return <span className={`opacity-0 ${className}`}>...</span>;
    }

    try {
        if (relative) {
            return <span className={className}>{timeAgo(date)}</span>;
        }

        const d = new Date(date);
        return <span className={className}>{d.toLocaleDateString()}</span>;
    } catch (e) {
        return <span className={className}>{String(date)}</span>;
    }
}
