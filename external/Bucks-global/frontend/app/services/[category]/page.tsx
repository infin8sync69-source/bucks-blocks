"use client";

import React from 'react';
import { useParams, useRouter } from 'next/navigation';
import {
    FaTaxi, FaBriefcase, FaUtensils, FaBagShopping,
    FaWallet, FaTicket, FaTruckFast, FaUsers,
    FaBuildingColumns, FaNetworkWired, FaStar, FaChevronLeft
} from 'react-icons/fa6';

// Mapping slugs to icons and colors
const categoryConfig: Record<string, any> = {
    taxi: { icon: FaTaxi, label: 'Taxi', color: 'text-yellow-600', bg: 'bg-yellow-100' },
    jobs: { icon: FaBriefcase, label: 'Jobs', color: 'text-blue-600', bg: 'bg-blue-100' },
    foods: { icon: FaUtensils, label: 'Foods', color: 'text-orange-600', bg: 'bg-orange-100' },
    shopping: { icon: FaBagShopping, label: 'Shopping', color: 'text-pink-600', bg: 'bg-pink-100' },
    pay: { icon: FaWallet, label: 'Pay', color: 'text-purple-600', bg: 'bg-purple-100' },
    tickets: { icon: FaTicket, label: 'Book Tickets', color: 'text-green-600', bg: 'bg-green-100' },
    delivery: { icon: FaTruckFast, label: 'Delivery', color: 'text-red-600', bg: 'bg-red-100' },
    community: { icon: FaUsers, label: 'Community', color: 'text-teal-600', bg: 'bg-teal-100' },
    banking: { icon: FaBuildingColumns, label: 'Banking', color: 'text-indigo-600', bg: 'bg-indigo-100' },
    networks: { icon: FaNetworkWired, label: 'Networks', color: 'text-cyan-600', bg: 'bg-cyan-100' },
};

export default function ServiceCategoryPage() {
    const params = useParams();
    const router = useRouter();
    const categorySlug = params.category as string;
    const config = categoryConfig[categorySlug] || { icon: FaUsers, label: 'Service', color: 'text-gray-600', bg: 'bg-gray-100' };
    const Icon = config.icon;

    // Mock data for providers
    const providers = [
        { name: "EcoRide Taxi", rating: 4.8, jobs: 1240, price: "$$", status: "Available" },
        { name: "Urban Cabs", rating: 4.5, jobs: 890, price: "$", status: "Busy" },
        { name: "Luxury Limo", rating: 4.9, jobs: 450, price: "$$$", status: "Available" },
        { name: "City Hopper", rating: 4.2, jobs: 2100, price: "$", status: "Available" },
        { name: "Night Owl", rating: 4.7, jobs: 670, price: "$$", status: "Offline" },
    ];

    return (
        <div className="min-h-screen bg-gray-50 pb-20">
            {/* Header */}
            <div className={`pt-12 pb-6 px-6 ${config.bg} rounded-b-[2.5rem] shadow-sm relative overflow-hidden`}>
                <button
                    onClick={() => router.back()}
                    className="absolute top-6 left-6 w-8 h-8 bg-white/50 backdrop-blur rounded-full flex items-center justify-center text-gray-700 hover:bg-white transition"
                >
                    <FaChevronLeft className="text-xs" />
                </button>

                <div className="flex flex-col items-center justify-center">
                    <div className="w-12 h-12 bg-white rounded-2xl flex items-center justify-center shadow-sm mb-3">
                        <Icon className={`text-2xl ${config.color}`} />
                    </div>
                    <h1 className="text-2xl font-bold text-gray-900">{config.label} Services</h1>
                    <p className="text-sm text-gray-600 opacity-80">Find local {config.label.toLowerCase()} providers</p>
                </div>
            </div>

            {/* Content List */}
            <div className="px-5 mt-6 space-y-4">
                <div className="flex justify-between items-center px-1">
                    <h2 className="font-bold text-lg text-gray-800">Top Providers</h2>
                    <span className="text-xs text-primary font-bold">See All</span>
                </div>

                {providers.map((provider, idx) => (
                    <div key={idx} className="bg-white p-4 rounded-2xl shadow-sm border border-gray-100 flex items-center gap-4">
                        <div className={`w-12 h-12 rounded-full flex items-center justify-center text-lg font-bold text-white ${idx % 2 === 0 ? 'bg-gradient-to-br from-blue-400 to-indigo-500' : 'bg-gradient-to-br from-pink-400 to-rose-500'}`}>
                            {provider.name[0]}
                        </div>
                        <div className="flex-1">
                            <h3 className="font-bold text-gray-900">{provider.name}</h3>
                            <div className="flex items-center gap-2 text-xs text-gray-500">
                                <span className="flex items-center gap-1 text-yellow-500 font-bold">
                                    <FaStar /> {provider.rating}
                                </span>
                                <span>• {provider.jobs} jobs</span>
                                <span>• {provider.price}</span>
                            </div>
                        </div>
                        <div>
                            <span className={`text-[10px] font-bold px-2 py-1 rounded-full ${provider.status === 'Available' ? 'bg-green-100 text-green-600' :
                                    provider.status === 'Busy' ? 'bg-yellow-100 text-yellow-600' : 'bg-gray-100 text-gray-400'
                                }`}>
                                {provider.status}
                            </span>
                        </div>
                    </div>
                ))}
            </div>

            {/* Promo */}
            <div className="mx-5 mt-8 p-4 bg-gray-900 rounded-2xl text-white text-center">
                <p className="font-bold text-sm">Want to offer services?</p>
                <p className="text-xs text-gray-400 mb-3">Register as a provider on the swarm network.</p>
                <button className="bg-white text-black text-xs font-bold px-4 py-2 rounded-lg w-full">
                    Register Provider
                </button>
            </div>
        </div>
    );
}
