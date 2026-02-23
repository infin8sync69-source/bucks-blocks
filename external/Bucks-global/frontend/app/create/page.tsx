"use client";

import React, { useState, useRef } from 'react';
import { useRouter, useSearchParams } from 'next/navigation';
import { FaArrowLeft, FaXmark, FaCheck, FaGlobe, FaUserGroup, FaImage } from 'react-icons/fa6';
import { uploadFile } from '@/lib/api';

export default function Upload() {
    const router = useRouter();
    const searchParams = useSearchParams();
    const isStory = searchParams?.get('type') === 'story';
    const [step, setStep] = useState(1); // 1: Select, 2: Compose
    const [file, setFile] = useState<File | null>(null);
    const [caption, setCaption] = useState('');
    const [visibility, setVisibility] = useState<'public' | 'connections'>('public');
    const [isUploading, setIsUploading] = useState(false);
    const [error, setError] = useState('');
    const [previewUrl, setPreviewUrl] = useState<string | null>(null);
    const fileInputRef = useRef<HTMLInputElement>(null);

    const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
        if (e.target.files && e.target.files[0]) {
            const selectedFile = e.target.files[0];
            setFile(selectedFile);
            setError('');

            // Create preview
            if (selectedFile.type.startsWith('image/') || selectedFile.type.startsWith('video/')) {
                setPreviewUrl(URL.createObjectURL(selectedFile));
                setStep(2); // Auto-advance to next step
            } else {
                setPreviewUrl(null);
                setStep(2);
            }
        }
    };

    const handleDrop = (e: React.DragEvent) => {
        e.preventDefault();
        if (e.dataTransfer.files && e.dataTransfer.files[0]) {
            const selectedFile = e.dataTransfer.files[0];
            setFile(selectedFile);
            if (selectedFile.type.startsWith('image/') || selectedFile.type.startsWith('video/')) {
                setPreviewUrl(URL.createObjectURL(selectedFile));
                setStep(2);
            }
        }
    };

    const clearFile = () => {
        setFile(null);
        setPreviewUrl(null);
        setError('');
        setStep(1);
    };

    const handleSubmit = async () => {
        if (!file) return;

        setIsUploading(true);
        setError('');

        const formData = new FormData();
        formData.append('file', file);
        // Use caption as title to support legacy backend, description empty for now
        formData.append('title', caption || 'Untitled Post');
        formData.append('description', caption);
        formData.append('visibility', visibility);

        try {
            await uploadFile(formData);
            router.push('/feed');
        } catch (err) {
            console.error('Upload failed', err);
            setError('Failed to upload file. Please try again.');
            setIsUploading(false);
        }
    };

    // --- STEP 1: MEDIA SELECTION ---
    if (step === 1) {
        return (
            <div
                className="min-h-screen bg-white flex flex-col items-center justify-center p-6 relative"
                onDragOver={(e) => e.preventDefault()}
                onDrop={handleDrop}
            >
                {/* Cancel / Back button */}
                <button
                    onClick={() => router.back()}
                    className="absolute top-4 left-4 p-2 rounded-full bg-gray-100 hover:bg-gray-200 text-gray-600 transition-colors"
                    aria-label="Cancel"
                >
                    <FaXmark className="text-lg" />
                </button>

                <div className="text-center space-y-6 max-w-sm w-full animate-in fade-in zoom-in-95 duration-500">
                    <div
                        onClick={() => fileInputRef.current?.click()}
                        className="w-32 h-32 mx-auto bg-gray-50 rounded-full flex items-center justify-center cursor-pointer hover:bg-gray-100 hover:scale-105 transition-all shadow-xl shadow-primary/5 active:scale-95 group"
                    >
                        <FaImage className="text-4xl text-gray-300 group-hover:text-primary transition-colors" />
                    </div>

                    <div>
                        <h1 className="text-2xl font-black text-gray-900 tracking-tight mb-2">
                            {isStory ? 'Add to Story' : 'Create New Post'}
                        </h1>
                        <p className="text-gray-500 text-sm">Drag photos and videos here</p>
                    </div>

                    <button
                        onClick={() => fileInputRef.current?.click()}
                        className="w-full py-4 bg-primary text-white rounded-2xl font-bold text-sm hover:opacity-90 transition-opacity shadow-lg shadow-primary/20"
                    >
                        Select from Device
                    </button>

                    <input
                        ref={fileInputRef}
                        type="file"
                        accept="image/*,video/*"
                        onChange={handleFileChange}
                        className="hidden"
                    />
                </div>
            </div>
        );
    }

    // --- STEP 2: COMPOSE ---
    return (
        <div className="min-h-screen bg-gray-50 md:flex md:items-center md:justify-center p-0 md:p-6">
            <div className="w-full max-w-4xl bg-white md:rounded-3xl shadow-2xl overflow-hidden flex flex-col md:flex-row h-screen md:h-[600px] animate-in fade-in slide-in-from-bottom-5 duration-500">

                {/* Visual Side (Left) */}
                <div className="relative w-full md:w-[60%] bg-black flex items-center justify-center group">
                    {previewUrl ? (
                        file?.type.startsWith('video/') ? (
                            <video src={previewUrl} className="max-w-full max-h-full object-contain" autoPlay muted loop />
                        ) : (
                            <img src={previewUrl} alt="Preview" className="max-w-full max-h-full object-contain" />
                        )
                    ) : (
                        <div className="text-white">No Preview</div>
                    )}

                    <button
                        onClick={clearFile}
                        className="absolute top-4 left-4 p-3 bg-black/50 text-white rounded-full hover:bg-black/70 backdrop-blur-md z-10"
                    >
                        <FaArrowLeft />
                    </button>
                </div>

                {/* Input Side (Right) */}
                <div className="w-full md:w-[40%] flex flex-col bg-white border-l border-gray-100">
                    {/* Header */}
                    <div className="px-6 py-4 border-b border-gray-100 flex items-center justify-between">
                        <span className="font-bold text-sm">New Post</span>
                        <button
                            onClick={handleSubmit}
                            disabled={isUploading}
                            className="text-sm font-bold text-primary hover:text-primary/80 disabled:opacity-50"
                        >
                            {isUploading ? 'Sharing...' : 'Share'}
                        </button>
                    </div>

                    {/* Caption Input */}
                    <div className="p-4 flex-1">
                        <div className="flex space-x-3 items-start">
                            <div className="w-8 h-8 bg-gray-100 rounded-full flex-shrink-0" />
                            <textarea
                                value={caption}
                                onChange={(e) => setCaption(e.target.value)}
                                placeholder="Write a caption..."
                                className="w-full h-32 resize-none outline-none text-sm placeholder:text-gray-400 leading-relaxed"
                                autoFocus
                            />
                        </div>

                        <div className="h-px bg-gray-100 my-4" />

                        {/* Settings */}
                        <div className="space-y-4">
                            <div className="flex items-center justify-between">
                                <span className="text-sm text-gray-600">Add Location</span>
                                <span className="text-xs text-gray-300">Coming soon</span>
                            </div>

                            <div className="flex flex-col space-y-2">
                                <span className="text-sm text-gray-600">Visibility</span>
                                <div className="flex space-x-2">
                                    <button
                                        onClick={() => setVisibility('public')}
                                        className={`flex-1 py-2 px-3 rounded-lg text-xs font-bold border transition-all flex items-center justify-center space-x-2 
                                            ${visibility === 'public' ? 'border-primary text-primary bg-primary/5' : 'border-gray-200 text-gray-500'}`}
                                    >
                                        <FaGlobe /> <span>Public</span>
                                    </button>
                                    <button
                                        onClick={() => setVisibility('connections')}
                                        className={`flex-1 py-2 px-3 rounded-lg text-xs font-bold border transition-all flex items-center justify-center space-x-2
                                            ${visibility === 'connections' ? 'border-primary text-primary bg-primary/5' : 'border-gray-200 text-gray-500'}`}
                                    >
                                        <FaUserGroup /> <span>Friends</span>
                                    </button>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            {error && (
                <div className="fixed top-6 left-1/2 -translate-x-1/2 bg-red-500 text-white px-6 py-3 rounded-full text-sm font-bold shadow-xl animate-in fade-in slide-in-from-top-5">
                    {error}
                </div>
            )}
        </div>
    );
}
