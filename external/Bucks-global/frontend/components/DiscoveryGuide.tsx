"use client";

import React from 'react';
import { FaUserPlus, FaHeartPulse, FaCompass, FaArrowRight } from 'react-icons/fa6';
import Link from 'next/link';

const DiscoveryGuide = () => {
    const steps = [
        {
            icon: <FaUserPlus className="text-blue-400" />,
            title: "Follow Peers",
            description: "Find your friends or interesting creators in the swarm and follow them."
        },
        {
            icon: <FaHeartPulse className="text-red-400" />,
            title: "Peer Vouching",
            description: "When they recommend content, it will instantly appear in your private hub."
        },
        {
            icon: <FaCompass className="text-purple-400" />,
            title: "Discover Services",
            description: "Explore decentralized services and verified high-quality media."
        }
    ];

    return (
        <div className="bg-white/50 backdrop-blur-xl border border-gray-100 rounded-3xl p-8 shadow-2xl animate-in fade-in zoom-in-95 duration-700">
            <div className="text-center mb-10">
                <div className="inline-flex items-center justify-center w-16 h-16 bg-primary/10 rounded-2xl mb-4 border border-primary/20">
                    <FaCompass className="text-3xl text-primary animate-pulse" />
                </div>
                <h2 className="text-2xl font-black text-gray-900 tracking-tight">Your Discovery Hub</h2>
                <p className="text-gray-500 text-sm mt-2 max-w-sm mx-auto font-medium">
                    This is your personal filter for the swarm. Here's how to fill it up:
                </p>
            </div>

            <div className="space-y-6 mb-10">
                {steps.map((step, i) => (
                    <div key={i} className="flex items-start space-x-4 p-4 rounded-2xl hover:bg-white transition-colors border border-transparent hover:border-gray-100">
                        <div className="text-xl mt-1 shrink-0 bg-gray-50 w-10 h-10 rounded-xl flex items-center justify-center border border-gray-100 shadow-sm">
                            {step.icon}
                        </div>
                        <div>
                            <h3 className="text-sm font-bold text-gray-800">{step.title}</h3>
                            <p className="text-gray-500 text-xs mt-1 leading-relaxed">
                                {step.description}
                            </p>
                        </div>
                    </div>
                ))}
            </div>

            <Link
                href="/feed"
                className="group w-full py-4 bg-primary text-white rounded-2xl font-black text-sm tracking-widest uppercase flex items-center justify-center space-x-2 shadow-xl shadow-primary/20 hover:scale-[1.02] active:scale-95 transition-all"
            >
                <span>Find Peers to Follow</span>
                <FaArrowRight className="text-xs group-hover:translate-x-1 transition-transform" />
            </Link>
        </div>
    );
};

export default DiscoveryGuide;
