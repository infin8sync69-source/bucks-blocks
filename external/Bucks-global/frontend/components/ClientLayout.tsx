"use client";

import React from 'react';
import { usePathname } from 'next/navigation';
import TopRightActions from '@/components/TopRightActions';
import Sidebar from '@/components/Sidebar';
import { fetchProfile } from '@/lib/api';

// Pages where we hide navigation chrome for focused flows
const FULL_SCREEN_PAGES = ['/login', '/create', '/recover', '/qr-scan'];

export default function ClientLayout({ children }: { children: React.ReactNode }) {
    const pathname = usePathname();
    const isHome = pathname === '/';
    const isFullScreen = FULL_SCREEN_PAGES.some(p => pathname.startsWith(p));

    React.useEffect(() => {
        // Fetch profile once on mount to populate localStorage cache
        if (typeof window !== 'undefined' && localStorage.getItem('bucks_peer_id')) {
            fetchProfile().catch(console.error);
        }
    }, [pathname]);

    return (
        <div className="min-h-screen relative bg-gray-50/50">
            {/* Top-right actions (notifications, search) — hide on full-screen pages */}
            {!isFullScreen && <TopRightActions />}

            {/* Sidebar — logic handles mobile hamburger + desktop sidebar */}
            {/* Hide sidebar completely on full-screen pages */}
            {!isFullScreen && <Sidebar />}

            {/* Main Content Area */}
            {/* 
                - Mobile: No padding needed (hamburger overlays)
                - Desktop: pl-64 (Sidebar width) ONLY if not full-screen
            */}
            <div className={`transition-all duration-300 ${!isFullScreen ? 'md:pl-64' : ''
                }`}>
                <div className="flex justify-center min-h-screen">
                    <main className={`w-full min-h-screen ${!isFullScreen ? 'md:max-w-2xl' : ''
                        }`}>
                        <div key={pathname} className="animate-in fade-in zoom-in-95 duration-300">
                            {children}
                        </div>
                    </main>
                </div>
            </div>
        </div>
    );
}
