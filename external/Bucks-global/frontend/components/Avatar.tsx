import React from 'react';
import { getIPFSUrl } from '../lib/api';

interface AvatarProps {
    src?: string;
    seed?: string; // Peer ID or username to generate deterministic pattern
    size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl';
    className?: string;
}

const Avatar: React.FC<AvatarProps> = ({ src, seed = 'anonymous', size = 'md', className = '' }) => {
    const sizeMap = {
        'xs': 'w-6 h-6 text-[8px]',
        'sm': 'w-8 h-8 text-[10px]',
        'md': 'w-10 h-10 text-xs',
        'lg': 'w-14 h-14 text-sm',
        'xl': 'w-20 h-20 text-base',
    };

    const [imgError, setImgError] = React.useState(false);

    // If src is provided and not errored, try to show image
    if (src && src !== 'ipfs://' && !src.includes('broken') && !imgError && src !== "🌐") {
        const ipfsUrl = src.startsWith('http') ? src : getIPFSUrl(src.replace('ipfs://', ''));
        return (
            <div className={`${sizeMap[size]} rounded-full overflow-hidden border border-white/10 shadow-inner flex-shrink-0 ${className}`}>
                <img
                    src={ipfsUrl}
                    alt="avatar"
                    className="w-full h-full object-cover"
                    onError={() => setImgError(true)}
                />
            </div>
        );
    }

    // Generate deterministic background color and pattern
    const getHash = (str: string) => {
        let hash = 0;
        for (let i = 0; i < str.length; i++) {
            hash = str.charCodeAt(i) + ((hash << 5) - hash);
        }
        return hash;
    };

    const hash = getHash(seed);
    const hue = Math.abs(hash % 360);
    const bgColor = `hsl(${hue}, 45%, 45%)`;
    const bgColorLight = `hsl(${hue}, 55%, 60%)`;
    const accentColor = `hsl(${(hue + 40) % 360}, 70%, 65%)`;

    return (
        <div
            className={`${sizeMap[size]} rounded-full flex-shrink-0 flex items-center justify-center text-white font-bold relative overflow-hidden shadow-xl ${className} border-2 border-white/20`}
            style={{ background: `linear-gradient(135deg, ${bgColorLight}, ${bgColor})` }}
        >
            {/* Soft Glow */}
            <div className="absolute inset-0 bg-white/10 backdrop-blur-[1px]" />

            {/* Deterministic Geometric Pattern */}
            <svg className="absolute inset-0 w-full h-full opacity-40 mix-blend-overlay" viewBox="0 0 100 100" preserveAspectRatio="none">
                <rect x="0" y="0" width="50" height="50" fill={accentColor} transform={`rotate(${hash % 90} 50 50)`} />
                <circle cx="80" cy="20" r="30" fill="white" className="opacity-20" />
                <path d="M0 100 L50 0 L100 100 Z" fill={accentColor} className="opacity-30" />
                <circle cx="50" cy="50" r="40" stroke="white" strokeWidth="1" fill="none" className="opacity-10" />
            </svg>

            <span className="relative z-10 uppercase drop-shadow-md tracking-tighter">
                {seed.substring(0, 2)}
            </span>

            {/* Glass Highlight */}
            <div className="absolute top-0 left-0 w-full h-1/2 bg-gradient-to-b from-white/20 to-transparent" />
        </div>
    );
};

export default Avatar;
