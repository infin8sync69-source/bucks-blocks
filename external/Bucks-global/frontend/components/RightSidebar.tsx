"use client";

import React from 'react';
import Link from 'next/link';
import { usePathname } from 'next/navigation';
import {
    FaHouse, FaRss, FaCube, FaHeart, FaUser,
    FaMessage, FaRegBell, FaGear, FaArrowRightFromBracket
} from 'react-icons/fa6';

const RightSidebar = () => {
    const pathname = usePathname();

    const navItems = [
        { label: 'Home', icon: <FaHouse />, href: '/' },
        { label: 'Feed', icon: <FaRss />, href: '/feed' },
        { label: 'Services', icon: <FaCube />, href: '/services' },
        { label: 'Recommended', icon: <FaHeart />, href: '/recommended' },
        { label: 'Profile', icon: <FaUser />, href: '/profile' },
    ];

    return (
        <div className="hidden md:flex flex-col fixed right-0 top-0 bottom-0 w-64 bg-white border-l border-gray-100 z-50 p-6">
            {/* Logo Area */}
            <div className="flex items-center justify-center mb-10">
                <Link href="/" className="font-black text-2xl text-primary tracking-tight">
                    bucks
                </Link>
            </div>

            {/* Navigation Links */}
            <nav className="flex-1 space-y-2">
                {navItems.map((item) => {
                    const isActive = pathname === item.href;
                    return (
                        <Link
                            key={item.href}
                            href={item.href}
                            className={`flex items-center space-x-4 px-4 py-3 rounded-2xl transition-all duration-200 group ${isActive
                                    ? 'bg-primary text-white shadow-lg shadow-primary/20'
                                    : 'text-secondary hover:bg-gray-50 hover:text-foreground'
                                }`}
                        >
                            <span className={`text-xl ${isActive ? 'scale-110' : 'group-hover:scale-110'} transition-transform`}>
                                {item.icon}
                            </span>
                            <span className="font-bold text-sm">{item.label}</span>
                        </Link>
                    );
                })}
            </nav>

            {/* Secondary Actions */}
            <div className="mt-auto space-y-4 pt-6 border-t border-gray-100">
                <div className="grid grid-cols-2 gap-3">
                    <Link href="/messages" className="flex flex-col items-center justify-center p-3 rounded-2xl bg-gray-50 hover:bg-gray-100 transition-colors text-secondary hover:text-primary relative group">
                        <FaMessage className="text-lg mb-1 group-hover:scale-110 transition-transform" />
                        <span className="text-[10px] font-bold">Messages</span>
                        <div className="absolute top-3 right-3 w-2 h-2 bg-red-500 rounded-full border-2 border-white"></div>
                    </Link>
                    <button className="flex flex-col items-center justify-center p-3 rounded-2xl bg-gray-50 hover:bg-gray-100 transition-colors text-secondary hover:text-primary group">
                        <FaRegBell className="text-lg mb-1 group-hover:scale-110 transition-transform" />
                        <span className="text-[10px] font-bold">Alerts</span>
                    </button>
                </div>

                <div className="space-y-1">
                    <Link href="/settings" className="flex items-center space-x-3 px-4 py-2 text-secondary hover:text-foreground hover:bg-gray-50 rounded-xl transition-colors">
                        <FaGear className="text-xs" />
                        <span className="text-xs font-bold">Settings</span>
                    </Link>
                    <button className="w-full flex items-center space-x-3 px-4 py-2 text-red-400 hover:text-red-500 hover:bg-red-50 rounded-xl transition-colors">
                        <FaArrowRightFromBracket className="text-xs" />
                        <span className="text-xs font-bold">Logout</span>
                    </button>
                </div>
            </div>
        </div>
    );
};

export default RightSidebar;
