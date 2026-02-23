"use client";

import { useEffect, useState } from 'react';
import { useRouter, usePathname } from 'next/navigation';

export default function AuthGuard({ children }: { children: React.ReactNode }) {
    const router = useRouter();
    const pathname = usePathname();
    const [isAuthenticated, setIsAuthenticated] = useState<boolean | null>(null);

    useEffect(() => {
        const checkAuth = async () => {
            const did = localStorage.getItem('bucks_peer_id');
            const auth = localStorage.getItem('isAuthenticated') === 'true';

            if (did && auth) {
                setIsAuthenticated(true);

                // Only check onboarding if not already on settings/onboarding
                if (pathname !== '/settings' && pathname !== '/login') {
                    try {
                        const { fetchProfile } = await import('@/lib/api');
                        const profile = await fetchProfile();
                        if (profile.onboarding) {
                            router.push('/settings?onboarding=true');
                        }
                    } catch (err) {
                        console.error('Failed to fetch profile during auth check', err);
                    }
                }
            } else {
                setIsAuthenticated(false);
                if (pathname !== '/login' && pathname !== '/recover') {
                    router.push('/login');
                }
            }
        };

        checkAuth();

        window.addEventListener('storage', checkAuth);
        return () => window.removeEventListener('storage', checkAuth);
    }, [pathname, router]);

    // Show nothing or a loader while checking
    if (isAuthenticated === null) {
        return (
            <div className="min-h-screen flex items-center justify-center bg-white">
                <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary"></div>
            </div>
        );
    }

    return <>{children}</>;
}
