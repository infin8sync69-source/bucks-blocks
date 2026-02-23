"use client";

import React from 'react';
import Link from 'next/link';
import { FaMagnifyingGlass, FaMessage, FaRegBell } from 'react-icons/fa6';
import { usePathname } from 'next/navigation';

const TopRightActions = () => {
    const pathname = usePathname();
    const isHome = pathname === '/';

    const iconBase = (isHomeVariant: boolean) =>
        `flex items-center justify-center w-10 h-10 rounded-full transition-all duration-300 relative group ${isHomeVariant
            ? 'bg-white/10 hover:bg-white/20 text-white backdrop-blur-md border border-white/10'
            : 'bg-white hover:bg-gray-100 text-secondary hover:text-primary shadow-sm border border-gray-200'
        }`;

    return (
        <div className={`fixed top-4 right-4 md:top-6 md:right-6 z-[60] items-center space-x-2 
            ${isHome ? 'flex' : 'hidden md:flex'}
        `}>
            {/* Search */}
            <Link href="/search" className={iconBase(isHome)}>
                <FaMagnifyingGlass className="text-sm" />
            </Link>

            {/* Messages */}
            <Link href="/messages" className={iconBase(isHome)}>
                <FaMessage className="text-sm" />
                <div className="absolute top-2 right-2.5 w-2 h-2 bg-red-500 rounded-full border-2 border-transparent"></div>
            </Link>

            {/* Notifications */}
            <Link href="/notifications" className={iconBase(isHome)}>
                <FaRegBell className="text-sm" />
                <div className="absolute top-2 right-3 w-1.5 h-1.5 bg-primary rounded-full"></div>
            </Link>
        </div>
    );
};

export default TopRightActions;
