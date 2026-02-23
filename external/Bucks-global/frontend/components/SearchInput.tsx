import React, { useState } from 'react';
import { FaMagnifyingGlass, FaXmark } from 'react-icons/fa6';

interface SearchInputProps {
    initialQuery?: string;
    onSearch: (query: string) => void;
    placeholder?: string;
    className?: string;
}

const SearchInput: React.FC<SearchInputProps> = ({
    initialQuery = '',
    onSearch,
    placeholder = 'Search...',
    className = ''
}) => {
    const [query, setQuery] = useState(initialQuery);

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault();
        onSearch(query);
    };

    const clearSearch = () => {
        setQuery('');
        onSearch('');
    };

    return (
        <form onSubmit={handleSubmit} className={`relative flex items-center ${className}`}>
            <FaMagnifyingGlass className="absolute left-4 text-gray-400" />
            <input
                type="text"
                value={query}
                onChange={(e) => setQuery(e.target.value)}
                placeholder={placeholder}
                className="w-full bg-gray-100 hover:bg-gray-200 focus:bg-white border-transparent focus:border-primary border-2 text-gray-900 placeholder-gray-500 rounded-full py-2.5 pl-11 pr-10 transition-all outline-none"
            />
            {query && (
                <button
                    type="button"
                    onClick={clearSearch}
                    className="absolute right-3 text-gray-400 hover:text-gray-600 p-1"
                >
                    <FaXmark />
                </button>
            )}
        </form>
    );
};

export default SearchInput;
