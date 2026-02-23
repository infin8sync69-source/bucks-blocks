"use client";

import React, { createContext, useContext, useState, useCallback, ReactNode } from 'react';
import { FaCircleCheck, FaCircleExclamation, FaCircleInfo, FaXmark } from 'react-icons/fa6';

type ToastType = 'success' | 'error' | 'info';

interface Toast {
    id: string;
    message: string;
    type: ToastType;
}

interface ToastContextType {
    showToast: (message: string, type?: ToastType) => void;
}

const ToastContext = createContext<ToastContextType | undefined>(undefined);

export const useToast = () => {
    const context = useContext(ToastContext);
    if (!context) throw new Error('useToast must be used within a ToastProvider');
    return context;
};

export const ToastProvider = ({ children }: { children: ReactNode }) => {
    const [toasts, setToasts] = useState<Toast[]>([]);

    const showToast = useCallback((message: string, type: ToastType = 'success') => {
        const id = Math.random().toString(36).substring(2, 9);
        setToasts(prev => [...prev, { id, message, type }]);
        setTimeout(() => {
            setToasts(prev => prev.filter(t => t.id !== id));
        }, 3000);
    }, []);

    const removeToast = (id: string) => {
        setToasts(prev => prev.filter(t => t.id !== id));
    };

    return (
        <ToastContext.Provider value={{ showToast }}>
            {children}
            <div className="fixed bottom-24 left-1/2 -translate-x-1/2 z-[100] flex flex-col items-center space-y-2 w-full max-w-sm px-4">
                {toasts.map(toast => (
                    <div
                        key={toast.id}
                        className={`w-full flex items-center justify-between px-4 py-3 rounded-2xl shadow-xl border animate-in slide-in-from-bottom-5 fade-in duration-300 ${toast.type === 'success' ? 'bg-green-50 border-green-100 text-green-800' :
                            toast.type === 'error' ? 'bg-red-50 border-red-100 text-red-800' :
                                'bg-blue-50 border-blue-100 text-blue-800'
                            }`}
                    >
                        <div className="flex items-center space-x-3">
                            {toast.type === 'success' && <FaCircleCheck className="text-green-500" />}
                            {toast.type === 'error' && <FaCircleExclamation className="text-red-500" />}
                            {toast.type === 'info' && <FaCircleInfo className="text-blue-500" />}
                            <span className="text-sm font-medium">{toast.message}</span>
                        </div>
                        <button onClick={() => removeToast(toast.id)} className="text-current opacity-60 hover:opacity-100 p-1">
                            <FaXmark className="text-xs" />
                        </button>
                    </div>
                ))}
            </div>
        </ToastContext.Provider>
    );
};
