"use client";

import React, { useState } from 'react';
import Link from 'next/link';
import { useRouter } from 'next/navigation';
import {
    FaTaxi, FaBriefcase, FaUtensils, FaBagShopping,
    FaWallet, FaTicket, FaTruckFast, FaUsers,
    FaBuildingColumns, FaNetworkWired, FaLock, FaMapLocationDot
} from 'react-icons/fa6';
import { useToast } from '@/components/Toast';

export default function Services() {
    const router = useRouter(); // Import useRouter

    const services = [
        { icon: FaTaxi, label: 'Taxi', gradient: 'from-amber-100 to-amber-200 text-amber-600', locked: true, slug: 'taxi' },
        { icon: FaBriefcase, label: 'Jobs', gradient: 'from-blue-100 to-blue-200 text-blue-600', locked: true, slug: 'jobs' },
        { icon: FaUtensils, label: 'Foods', gradient: 'from-orange-100 to-orange-200 text-orange-600', locked: true, slug: 'foods' },
        { icon: FaBagShopping, label: 'Shopping', gradient: 'from-pink-100 to-pink-200 text-pink-600', locked: true, slug: 'shopping' },
        { icon: FaWallet, label: 'Pay', gradient: 'from-purple-100 to-purple-200 text-purple-600', locked: true, slug: 'pay' },
        { icon: FaTicket, label: 'Tickets', gradient: 'from-emerald-100 to-emerald-200 text-emerald-600', locked: true, slug: 'tickets' },
        { icon: FaTruckFast, label: 'Delivery', gradient: 'from-red-100 to-red-200 text-red-600', locked: true, slug: 'delivery' },
        { icon: FaUsers, label: 'Community', gradient: 'from-teal-100 to-teal-200 text-teal-600', locked: true, slug: 'community' },
        { icon: FaBuildingColumns, label: 'Banking', gradient: 'from-indigo-100 to-indigo-200 text-indigo-600', locked: true, slug: 'banking' },
        { icon: FaNetworkWired, label: 'Networks', gradient: 'from-cyan-100 to-cyan-200 text-cyan-600', locked: true, slug: 'networks' },
    ];

    const { showToast } = useToast();

    const handleServiceClick = (service: any) => {
        if (service.locked) {
            showToast(`Service "${service.label}" is coming soon!`, 'info');
        } else {
            router.push(`/services/${service.slug}`);
        }
    };

    return (
        <div className="min-h-screen bg-white pb-24 md:pb-0">
            {/* Map Header */}
            <div className="relative w-full h-64 bg-gray-200 overflow-hidden">
                {/* Mock Map Background - Using a placeholder image or CSS pattern */}
                <div className="absolute inset-0 opacity-60" style={{
                    backgroundImage: 'url("https://maps.googleapis.com/maps/api/staticmap?center=San+Francisco,CA&zoom=13&size=600x300&maptype=roadmap&style=feature:all|saturation:-100&key=YOUR_API_KEY")', // Using a generic map-like look via CSS if image fails
                    backgroundSize: 'cover',
                    backgroundPosition: 'center',
                    backgroundColor: '#e5e7eb'
                }}>
                    <div className="w-full h-full bg-[radial-gradient(#cbd5e1_1px,transparent_1px)] [background-size:16px_16px] opacity-50"></div>
                </div>

                {/* Map Pins */}
                <div className="absolute top-1/2 left-1/3 transform -translate-x-1/2 -translate-y-1/2">
                    <div className="w-8 h-8 bg-primary rounded-full border-4 border-white shadow-lg animate-bounce flex items-center justify-center">
                        <FaMapLocationDot className="text-white text-xs" />
                    </div>
                </div>
                <div className="absolute top-1/3 right-1/4 transform -translate-x-1/2 -translate-y-1/2">
                    <div className="w-6 h-6 bg-purple-500 rounded-full border-2 border-white shadow-md"></div>
                </div>

                {/* Gradient Overlay */}
                <div className="absolute bottom-0 left-0 right-0 h-24 bg-gradient-to-t from-white to-transparent"></div>
            </div>

            {/* Content Container - Pull up over map */}
            <div className="relative -mt-6 rounded-t-[2.5rem] bg-white px-6 pt-8 pb-12 shadow-[0_-10px_40px_rgba(0,0,0,0.05)]">
                {/* Drag Handle */}
                <div className="w-12 h-1.5 bg-gray-200 rounded-full mx-auto mb-8"></div>

                <h1 className="text-3xl font-bold mb-2 pl-2">Services</h1>
                <p className="text-gray-500 mb-8 pl-2 text-sm">Discover decentralized apps & local services</p>

                <div className="grid grid-cols-4 gap-y-8 gap-x-4">
                    {services.map((service, index) => (
                        <div
                            key={index}
                            onClick={() => handleServiceClick(service)}
                            className="flex flex-col items-center group cursor-pointer relative"
                        >
                            <div className="relative">
                                <div className={`
                                    w-16 h-16 rounded-[1.5rem] flex items-center justify-center mb-3 transition-all duration-300
                                    bg-gradient-to-br ${service.gradient} shadow-lg shadow-gray-200/50
                                    group-hover:scale-105 group-hover:-translate-y-1 relative overflow-hidden border border-white
                                `}>
                                    <div className="absolute inset-0 bg-white/20 opacity-0 group-hover:opacity-100 transition-opacity"></div>
                                    <service.icon className="text-2xl drop-shadow-sm z-10" />
                                </div>

                                {service.locked && (
                                    <div className="absolute -top-2 -right-2 w-7 h-7 bg-white/95 backdrop-blur-md rounded-full flex items-center justify-center shadow-[0_4px_12px_rgba(0,0,0,0.15)] border border-gray-100 z-20 group-hover:scale-110 transition-transform duration-300">
                                        <FaLock className="text-[10px] text-gray-400" />
                                    </div>
                                )}
                            </div>
                            <span className="text-[11px] font-bold text-center tracking-tight text-gray-500 group-hover:text-primary transition-colors">
                                {service.label}
                            </span>
                        </div>
                    ))}
                </div>

                {/* Promo Banner */}
                <div className="mt-12 mx-auto bg-gradient-to-r from-violet-500 to-fuchsia-500 rounded-3xl p-6 text-white shadow-lg relative overflow-hidden">
                    <div className="absolute top-0 right-0 -mt-4 -mr-4 w-24 h-24 bg-white opacity-20 rounded-full blur-2xl"></div>
                    <div className="relative z-10">
                        <div className="flex items-center gap-2 mb-2">
                            <span className="bg-white/20 px-2 py-0.5 rounded text-[10px] font-bold uppercase tracking-wider">New</span>
                            <h3 className="font-bold text-lg">Build on IPFS</h3>
                        </div>
                        <p className="text-white/90 text-sm mb-4 max-w-[80%]">
                            Create your own decentralized service and reach the swarm instantly.
                        </p>
                        <button className="bg-white text-violet-600 px-4 py-2 rounded-xl text-xs font-bold shadow-sm hover:bg-gray-50 transition-colors">
                            Start Building
                        </button>
                    </div>
                </div>
            </div>
        </div>
    );
}
