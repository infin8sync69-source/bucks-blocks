import axios from 'axios';

// Helper to get the correct hostname for the local network (LAN)
const getHost = () => {
    if (typeof window !== 'undefined') {
        return window.location.hostname;
    }
    return 'localhost';
};

// API Client configuration
// Use the current host to ensure it works on the local network (WiFi)
const getBaseURL = () => `http://${getHost()}:8000/api`;
const getBlockchainURL = () => `http://${getHost()}:8080/api`;

export const api = axios.create({
    baseURL: getBaseURL(),
    headers: {
        'Content-Type': 'application/json',
    },
});

// Helper for Base58 encoding (simplified for did:key)
const ALPHABET = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
const base58Encode = (buffer: Uint8Array): string => {
    let digits = [0];
    for (let i = 0; i < buffer.length; i++) {
        for (let j = 0; j < digits.length; j++) digits[j] <<= 8;
        digits[0] += buffer[i];
        let carry = 0;
        for (let j = 0; j < digits.length; ++j) {
            digits[j] += carry;
            carry = (digits[j] / 58) | 0;
            digits[j] %= 58;
        }
        while (carry) {
            digits.push(carry % 58);
            carry = (carry / 58) | 0;
        }
    }
    for (let i = 0; buffer[i] === 0 && i < buffer.length - 1; i++) digits.push(0);
    return digits.reverse().map(digit => ALPHABET[digit]).join('');
};

const publicKeyToDid = (pubKeyHex: string): string => {
    const pubKeyBytes = new Uint8Array(pubKeyHex.match(/.{1,2}/g)!.map(byte => parseInt(byte, 16)));
    // Header for secp256k1 (0xe701)
    const header = new Uint8Array([0xe7, 0x01]);
    const full = new Uint8Array(header.length + pubKeyBytes.length);
    full.set(header);
    full.set(pubKeyBytes, header.length);
    return `did:key:z${base58Encode(full)}`;
};

// Helper to compute HMAC-SHA256 signature
const signRequest = async (secret: string, method: string, path: string, timestamp: string): Promise<string> => {
    const encoder = new TextEncoder();
    const keyData = encoder.encode(secret);
    const payload = `${method.toUpperCase()}${path}${timestamp}`;

    try {
        const key = await window.crypto.subtle.importKey(
            "raw",
            keyData,
            { name: "HMAC", hash: "SHA-256" },
            false,
            ["sign"]
        );

        const signature = await window.crypto.subtle.sign(
            "HMAC",
            key,
            encoder.encode(payload)
        );

        // Convert array buffer to hex string
        return Array.from(new Uint8Array(signature))
            .map(b => b.toString(16).padStart(2, '0'))
            .join('');
    } catch (e) {
        console.error("Signing failed", e);
        return "";
    }
};

// Inject X-DID header and signature for every request
api.interceptors.request.use(async (config) => {
    if (typeof window !== 'undefined') {
        const did = localStorage.getItem('bucks_peer_id');
        const secret = localStorage.getItem('bucks_identity_secret');
        const isWallet = localStorage.getItem('bucks_identity_type') === 'bucks_wallet';
        const walletAddress = localStorage.getItem('bucks_wallet_address');

        if (did) {
            config.headers['X-DID'] = did;

            const timestamp = Date.now().toString();
            const path = config.url?.replace(config.baseURL || '', '') || '';
            const payload = `${config.method?.toUpperCase() || 'GET'}${path}${timestamp}`;

            // 1. Bucks Wallet Remote Signing
            if (isWallet && walletAddress && config.url) {
                try {
                    const signRes = await axios.post(`${getBlockchainURL()}/wallets/sign`, {
                        address: walletAddress,
                        message: payload
                    });
                    if (signRes.data.success) {
                        config.headers['X-Signature'] = btoa(String.fromCharCode(...new Uint8Array(signRes.data.signature.match(/.{1,2}/g)!.map((byte: string) => parseInt(byte, 16)))));
                        config.headers['X-Timestamp'] = timestamp;
                        // Ensure DID matches the wallet public key
                        config.headers['X-DID'] = publicKeyToDid(signRes.data.publicKey);
                    }
                } catch (e) {
                    console.error("Wallet signing failed, falling back", e);
                }
            }
            // 2. Local Secret Signing (HMAC)
            else if (secret && config.url) {
                const signature = await signRequest(secret, config.method || 'GET', path, timestamp);
                if (signature) {
                    config.headers['X-Signature'] = signature;
                    config.headers['X-Timestamp'] = timestamp;
                }
            }
        }
    }
    return config;
});

export interface LibraryItem {
    cid: string;
    thumbnail_cid?: string;
    name: string;
    description: string;
    filename: string;
    type: string;
    author: string;
    avatar: string;
    timestamp: string;
    peer_id?: string;
    recommended_by?: string[];
    _is_social_discovery?: boolean;
}

export interface Interaction {
    recommended: boolean;
    not_recommended: boolean;
    comments: string[];
    views: number;
    likes_count: number;
    dislikes_count: number;
}

export interface UserProfile {
    username: string;
    handle?: string;
    avatar: string;
    banner?: string;
    bio: string;
    location?: string;
    tags?: string[];
    peer_id?: string;
    did?: string;
    stats: {
        syncs: number;
        contacts: number;
        posts?: number;
        likes?: number;
    };
    onboarding?: boolean;
}

export const fetchLibrary = async () => {
    const response = await api.get<{ library: LibraryItem[] }>('/library');
    return response.data.library;
};

export const fetchPost = async (cid: string) => {
    const response = await api.get<LibraryItem>(`/library/${cid}`);
    return response.data;
};

export const fetchInteractions = async (cid: string) => {
    const response = await api.get<Interaction>(`/interactions/${cid}`);
    return response.data;
};

export const fetchAllInteractions = async () => {
    const response = await api.get<Record<string, Interaction>>('/interactions');
    return response.data;
};

export const fetchUserLikes = async (peerId: string) => {
    const response = await api.get<{ likes: string[] }>(`/profile/${peerId}/likes`);
    return response.data.likes;
};

export const toggleLike = async (cid: string) => {
    const response = await api.post<Interaction>(`/interactions/${cid}/like`);
    return response.data;
};

export const toggleDislike = async (cid: string) => {
    const response = await api.post<Interaction>(`/interactions/${cid}/dislike`);
    return response.data;
};

export const addComment = async (cid: string, text: string) => {
    const response = await api.post<{ success: boolean; comment: string }>(`/interactions/${cid}/comment`, { text });
    return response.data;
};

export const deleteComment = async (cid: string, index: number) => {
    const response = await api.delete<{ success: boolean }>(`/interactions/${cid}/comment/${index}`);
    return response.data;
};

export const uploadFile = async (formData: FormData) => {
    const response = await api.post<{ cid: string; filename: string; success: boolean }>('/upload', formData, {
        headers: {
            'Content-Type': 'multipart/form-data',
        },
    });
    return response.data;
};

export const fetchProfile = async () => {
    const response = await api.get<UserProfile>('/profile');
    const profile = response.data;

    // Inject default stats if missing to prevent UI crashes
    if (!profile.stats) {
        profile.stats = { syncs: 0, contacts: 0, posts: 0, likes: 0 };
    }

    // Cache profile data for UI consistency
    if (typeof window !== 'undefined') {
        if (profile.peer_id) localStorage.setItem('bucks_peer_id', profile.peer_id);
        localStorage.setItem('bucks_user_profile', JSON.stringify(profile));
    }

    return profile;
};

export const fetchUserProfile = async (peer_id: string) => {
    const response = await api.get<UserProfile>(`/profile/${peer_id}`);
    return response.data;
};

export const fetchAggregatedFeed = async () => {
    const response = await api.get<{ library: LibraryItem[] }>('/feed/aggregated');
    return response.data;
};

export const fetchUserFeed = async (peer_id: string) => {
    const response = await api.get<{ library: LibraryItem[] }>(`/feed/user/${peer_id}`);
    return response.data.library;
};


export const deletePost = async (cid: string) => {
    const response = await api.delete<{ success: boolean; message: string }>(`/library/${cid}`);
    return response.data;
};

export const updatePost = async (cid: string, title: string, description: string) => {
    const formData = new FormData();
    formData.append('title', title);
    formData.append('description', description);
    const response = await api.put<{ success: boolean; post: LibraryItem }>(`/library/${cid}`, formData, {
        headers: { 'Content-Type': 'multipart/form-data' },
    });
    return response.data;
};

export const updateProfile = async (data: {
    username?: string;
    handle?: string;
    bio?: string;
    location?: string;
    avatar?: string;
    banner?: string;
}) => {
    const formData = new FormData();
    if (data.username) formData.append('username', data.username);
    if (data.handle) formData.append('handle', data.handle);
    if (data.bio) formData.append('bio', data.bio);
    if (data.location) formData.append('location', data.location);
    if (data.avatar) formData.append('avatar', data.avatar);
    if (data.banner) formData.append('banner', data.banner);

    const response = await api.post<{ success: boolean; profile: UserProfile }>('/profile', formData, {
        headers: { 'Content-Type': 'multipart/form-data' },
    });

    // Update local cache after successful save
    if (response.data.success && typeof window !== 'undefined') {
        const profile = response.data.profile;
        if (profile.peer_id) localStorage.setItem('bucks_peer_id', profile.peer_id);
        localStorage.setItem('bucks_user_profile', JSON.stringify(profile));
    }

    return response.data;
};

/**
 * Logout: Clear local identity and redirect to login
 */
export const logout = () => {
    if (typeof window !== 'undefined') {
        localStorage.removeItem('bucks_identity');
        localStorage.removeItem('bucks_peer_id');
        // Clear anything else relevant
        window.location.href = '/login';
    }
};

export const fetchFollowing = async () => {
    const response = await api.get<{ following: any[]; count: number }>('/following');
    return response.data.following;
};

export const followPeer = async (peerId: string) => {
    const response = await api.post(`/follow/${peerId}`);
    return response.data;
};

export const unfollowPeer = async (peerId: string) => {
    const response = await api.post(`/unfollow/${peerId}`);
    return response.data;
};

export const fetchConnections = async () => {
    const response = await api.get('/connections');
    return response.data;
};

export const sendConnectionRequest = async (peerId: string) => {
    const formData = new FormData();
    formData.append('peer_id', peerId);
    const response = await api.post('/connections/request', formData);
    return response.data;
};

export const acceptConnectionRequest = async (peerId: string) => {
    const formData = new FormData();
    formData.append('peer_id', peerId);
    const response = await api.post('/connections/accept', formData);
    return response.data;
};

export const discoverPeers = async () => {
    const response = await api.get('/peers/discover');
    return response.data;
};

export interface Notification {
    id: number;
    user_peer_id: string;
    type: 'message' | 'connection' | 'mention' | 'follow';
    title: string;
    message: string;
    link: string;
    timestamp: string;
    is_read: boolean;
}

export const fetchNotifications = async () => {
    const response = await api.get<{ notifications: Notification[] }>('/notifications');
    return response.data.notifications;
};

export const markNotificationsRead = async (notificationId?: number) => {
    if (notificationId) {
        const response = await api.post(`/notifications/${notificationId}/read`);
        return response.data;
    } else {
        const response = await api.post('/notifications/read-all');
        return response.data;
    }
};

export const getIPFSUrl = (cid: string) => {
    if (!cid) return '';
    const host = getHost();
    return `http://${host}:8080/ipfs/${cid}`;
};

export const fetchAgentResponse = async (query: string): Promise<string> => {
    // Basic implementation that routes to search or returns a simple response
    // since the dedicated agent endpoint is missing in this version.
    try {
        const response = await api.post<{ results: any[] }>('/search', { query });
        const count = response.data.results?.length || 0;
        return `Command received: ${query}. I found ${count} relevant items in the swarm index.`;
    } catch (e) {
        console.error("Agent Error", e);
        return "I'm having trouble connecting to the swarm agent right now.";
    }
};

export default api;
