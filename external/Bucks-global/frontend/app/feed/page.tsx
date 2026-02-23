"use client";

import React, { useEffect, useState } from 'react';
import { useSearchParams } from 'next/navigation';
import StoryCircles from '@/components/StoryCircles';
import PostCard from '@/components/PostCard';
import { LibraryItem, fetchAllInteractions, fetchAggregatedFeed, fetchFollowing } from '@/lib/api';
import {
  FaFilter, FaListUl, FaImage, FaVideo, FaFileLines,
  FaCheck, FaChevronDown, FaArrowDownShortWide,
  FaPlus,
  FaEarthAmericas, FaUsers, FaArrowUpWideShort
} from 'react-icons/fa6';
import Link from 'next/link';

export default function Feed() {
  const searchParams = useSearchParams();
  const filterCid = searchParams.get('cid');
  const filterAuthor = searchParams.get('author');

  const [library, setLibrary] = useState<LibraryItem[]>([]);
  const [following, setFollowing] = useState<string[]>([]);
  const [loading, setLoading] = useState(true);

  // Filter states
  const [filterType, setFilterType] = useState<'all' | 'images' | 'videos' | 'files'>('all');
  const [source, setSource] = useState<'global' | 'following'>('global');
  const [sortBy, setSortBy] = useState<'newest' | 'popular'>('newest');
  const [showFilterMenu, setShowFilterMenu] = useState(false);
  const [interactions, setInteractions] = useState<any>({});

  useEffect(() => {
    const loadData = async () => {
      try {
        const [feedData, followingData, interactionsRes] = await Promise.all([
          fetchAggregatedFeed(),
          fetchFollowing(),
          fetchAllInteractions()
        ]);

        setLibrary(feedData.library || []);
        // extract peer IDs from the following records if they are objects, but actually let's map them safely
        setFollowing((followingData || []).map((f: any) => f.following_peer_id || f));
        setInteractions(interactionsRes || {});
      } catch (error) {
        console.error('Failed to load feed data', error);
      } finally {
        setLoading(false);
      }
    };

    loadData();
  }, []);

  const filteredLibrary = library
    .filter(item => {
      if (filterCid && item.cid !== filterCid) return false;
      if (filterAuthor && item.author !== filterAuthor) return false;

      // Type Filter
      if (filterType === 'images') {
        const isImage = item.filename.toLowerCase().match(/\.(jpg|jpeg|png|gif|webp)$/i);
        if (!isImage) return false;
      }
      if (filterType === 'videos') {
        if (!item.filename.toLowerCase().endsWith('.mp4')) return false;
      }
      if (filterType === 'files') {
        const isMedia = item.filename.toLowerCase().match(/\.(jpg|jpeg|png|gif|webp|mp4)$/i);
        if (isMedia || item.type !== 'file') return false;
      }

      // Source Filter
      if (source === 'following') {
        if (!following.includes(item.author)) return false;
      }

      return true;
    })
    .sort((a, b) => {
      if (sortBy === 'newest') {
        return new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime();
      }
      if (sortBy === 'popular') {
        const engagementA = (interactions[a.cid]?.recommended ? 2 : 0) + (interactions[a.cid]?.comments?.length || 0);
        const engagementB = (interactions[b.cid]?.recommended ? 2 : 0) + (interactions[b.cid]?.comments?.length || 0);
        return engagementB - engagementA;
      }
      return 0;
    });

  const handlePostDeleted = (cid: string) => {
    setLibrary(prev => prev.filter(item => item.cid !== cid));
  };

  const handlePostUpdated = (cid: string, newTitle: string, newDescription: string) => {
    setLibrary(prev => prev.map(item =>
      item.cid === cid ? { ...item, name: newTitle, description: newDescription } : item
    ));
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center min-h-[50vh]">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary"></div>
      </div>
    );
  }

  return (
    <div className="flex flex-col min-h-full">
      <StoryCircles library={library} />

      {/* Advanced Filter Bar */}
      <div className="bg-white border-b border-gray-100 flex items-center justify-between px-4 py-2 sticky top-0 z-30 shadow-sm">
        <div className="flex items-center space-x-1">
          {[
            { id: 'all', icon: <FaListUl />, label: 'All' },
            { id: 'images', icon: <FaImage />, label: 'Images' },
            { id: 'videos', icon: <FaVideo />, label: 'Videos' },
            { id: 'files', icon: <FaFileLines />, label: 'Files' },
          ].map(type => (
            <button
              key={type.id}
              onClick={() => setFilterType(type.id as any)}
              className={`flex items-center space-x-1.5 px-3 py-1.5 rounded-full text-[10px] font-bold uppercase transition-all ${filterType === type.id
                ? 'bg-primary text-white shadow-md shadow-primary/20 scale-105'
                : 'text-gray-400 hover:bg-gray-100'}`}
            >
              {type.icon}
              <span className="hidden sm:inline">{type.label}</span>
            </button>
          ))}
        </div>

        <div className="relative">
          <button
            onClick={() => setShowFilterMenu(!showFilterMenu)}
            className={`flex items-center space-x-2 px-3 py-1.5 rounded-full text-[10px] font-bold uppercase border transition-all ${showFilterMenu || source !== 'global' || sortBy !== 'newest'
              ? 'bg-primary/5 border-primary text-primary shadow-sm'
              : 'border-gray-200 text-gray-500 hover:border-gray-300'}`}
          >
            <FaFilter className="text-[10px]" />
            <span>Filter</span>
            <FaChevronDown className={`text-[8px] transition-transform ${showFilterMenu ? 'rotate-180' : ''}`} />
          </button>

          {showFilterMenu && (
            <>
              <div className="fixed inset-0 z-40" onClick={() => setShowFilterMenu(false)}></div>
              <div className="absolute right-0 mt-2 w-56 bg-white border border-gray-100 rounded-2xl shadow-2xl py-2 z-50 animate-in fade-in zoom-in-95 duration-100">
                <div className="px-4 py-2 border-b border-gray-50">
                  <span className="text-[10px] font-bold text-gray-400 uppercase tracking-widest">Feed Source</span>
                  <div className="flex flex-col mt-2 space-y-1">
                    <button
                      onClick={() => { setSource('global'); setShowFilterMenu(false); }}
                      className="flex items-center justify-between px-2 py-1.5 rounded-lg text-sm hover:bg-gray-50 text-gray-700 transition-colors"
                    >
                      <div className="flex items-center space-x-2">
                        <FaEarthAmericas className={`text-gray-400 ${source === 'global' ? 'text-primary' : ''}`} />
                        <span className={source === 'global' ? 'font-bold' : ''}>Global Swarm</span>
                      </div>
                      {source === 'global' && <FaCheck className="text-primary text-[10px]" />}
                    </button>
                    <button
                      onClick={() => { setSource('following'); setShowFilterMenu(false); }}
                      className="flex items-center justify-between px-2 py-1.5 rounded-lg text-sm hover:bg-gray-50 text-gray-700 transition-colors"
                    >
                      <div className="flex items-center space-x-2">
                        <FaUsers className={`text-gray-400 ${source === 'following' ? 'text-primary' : ''}`} />
                        <span className={source === 'following' ? 'font-bold' : ''}>Following Only</span>
                      </div>
                      {source === 'following' && <FaCheck className="text-primary text-[10px]" />}
                    </button>
                  </div>
                </div>

                <div className="px-4 py-2">
                  <span className="text-[10px] font-bold text-gray-400 uppercase tracking-widest">Sort By</span>
                  <div className="flex flex-col mt-2 space-y-1">
                    <button
                      onClick={() => { setSortBy('newest'); setShowFilterMenu(false); }}
                      className="flex items-center justify-between px-2 py-1.5 rounded-lg text-sm hover:bg-gray-50 text-gray-700 transition-colors"
                    >
                      <div className="flex items-center space-x-2">
                        <FaArrowDownShortWide className={`text-gray-400 ${sortBy === 'newest' ? 'text-primary' : ''}`} />
                        <span className={sortBy === 'newest' ? 'font-bold' : ''}>Latest First</span>
                      </div>
                      {sortBy === 'newest' && <FaCheck className="text-primary text-[10px]" />}
                    </button>
                    <button
                      onClick={() => { setSortBy('popular'); setShowFilterMenu(false); }}
                      className="flex items-center justify-between px-2 py-1.5 rounded-lg text-sm hover:bg-gray-50 text-gray-700 transition-colors"
                    >
                      <div className="flex items-center space-x-2">
                        <FaArrowUpWideShort className={`text-gray-400 ${sortBy === 'popular' ? 'text-primary' : ''}`} />
                        <span className={sortBy === 'popular' ? 'font-bold' : ''}>Top Engagement</span>
                      </div>
                      {sortBy === 'popular' && <FaCheck className="text-primary text-[10px]" />}
                    </button>
                  </div>
                </div>
              </div>
            </>
          )}
        </div>
      </div>

      <div className="flex-1 bg-gray-50">
        {(filterCid || filterAuthor) && (
          <div className="px-4 py-2 bg-purple-50 flex items-center justify-between border-b border-purple-100">
            <span className="text-xs font-medium text-primary">
              Showing {filterCid ? 'selected post' : `posts by ${filterAuthor}`}
            </span>
            <button
              onClick={() => window.location.href = '/feed'}
              className="text-[10px] font-bold text-primary uppercase"
            >
              Clear Filter
            </button>
          </div>
        )}
        {filteredLibrary.length === 0 ? (
          <div className="text-center py-20 bg-white m-4 rounded-3xl border border-dashed border-gray-200">
            <div className="text-4xl mb-4">🛸</div>
            <h3 className="text-gray-900 font-bold">No content found</h3>
            <p className="text-gray-400 text-sm mt-1">Try adjusting your filters or source.</p>
            <button
              onClick={() => { setFilterType('all'); setSource('global'); setSortBy('newest'); }}
              className="mt-6 text-primary text-xs font-bold uppercase tracking-widest hover:underline"
            >
              Reset all filters
            </button>
          </div>
        ) : (
          filteredLibrary.map((item) => (
            <PostCard
              key={item.cid}
              item={item}
              onPostDeleted={handlePostDeleted}
              onPostUpdated={handlePostUpdated}
            />
          ))
        )}
      </div>

      {/* Floating Action Button */}
      <Link
        href="/create"
        className="fixed bottom-24 right-6 w-14 h-14 bg-primary text-white rounded-full shadow-2xl shadow-primary/40 flex items-center justify-center text-2xl hover:scale-110 hover:bg-primary/90 active:scale-95 transition-all z-40 md:bottom-10"
      >
        <FaPlus />
      </Link>
    </div>
  );
}
