"use client";

import React, { useEffect, useRef } from 'react';

const StarField = () => {
    const canvasRef = useRef<HTMLCanvasElement>(null);

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        if (!ctx) return;

        let animationFrameId: number;
        let stars: Star[] = [];
        let shootingStars: ShootingStar[] = [];
        // Deep space colors
        const bgGradient = ctx.createLinearGradient(0, 0, 0, canvas.height);
        bgGradient.addColorStop(0, '#020005'); // Deepest black-purple
        bgGradient.addColorStop(1, '#050010'); // Slightly lighter deep purple

        // Star properties
        const starCount = 300; // More stars for richness

        class Star {
            x: number;
            y: number;
            size: number;
            baseAlpha: number;
            alpha: number;
            pulseSpeed: number;
            pulseDirection: number;

            constructor(width: number, height: number, initial = false) {
                this.x = Math.random() * width;
                this.y = Math.random() * height;
                this.size = Math.random() < 0.9 ? Math.random() * 1 : Math.random() * 2 + 0.5; // Mostly small, some bright/big
                this.baseAlpha = Math.random() * 0.5 + 0.1;
                this.alpha = this.baseAlpha;
                this.pulseSpeed = Math.random() * 0.02 + 0.005;
                this.pulseDirection = 1;
            }

            update() {
                // Twinkle effect
                this.alpha += this.pulseSpeed * this.pulseDirection;
                if (this.alpha > this.baseAlpha + 0.3 || this.alpha < this.baseAlpha - 0.1) {
                    this.pulseDirection *= -1;
                }
            }

            draw(ctx: CanvasRenderingContext2D) {
                ctx.fillStyle = `rgba(255, 255, 255, ${Math.max(0, this.alpha)})`;
                ctx.beginPath();
                ctx.arc(this.x, this.y, this.size, 0, Math.PI * 2);
                ctx.fill();
            }
        }

        class ShootingStar {
            x: number;
            y: number;
            length: number;
            speed: number;
            angle: number;
            opacity: number;
            active: boolean;

            constructor(width: number, height: number) {
                this.x = Math.random() * width;
                this.y = Math.random() * height * 0.5; // Start mostly in upper half
                this.length = Math.random() * 80 + 20;
                this.speed = Math.random() * 10 + 5;
                this.angle = Math.PI / 4 + (Math.random() * 0.2 - 0.1); // Diagonal down-rightish
                this.opacity = 0;
                this.active = false;
            }

            reset(width: number, height: number) {
                this.x = Math.random() * width * 0.5 + width * 0.2; // Spawn somewhat centrally
                this.y = -50;
                this.length = Math.random() * 80 + 20;
                this.speed = Math.random() * 15 + 8;
                this.angle = Math.PI / 4 + (Math.random() * 0.5 - 0.25);
                this.opacity = 1;
                this.active = true;
            }

            update(width: number, height: number) {
                if (!this.active) {
                    // Random chance to spawn
                    if (Math.random() < 0.005) { // Adjust for frequency
                        this.reset(width, height);
                    }
                    return;
                }

                this.x += Math.cos(this.angle) * this.speed;
                this.y += Math.sin(this.angle) * this.speed;
                this.opacity -= 0.015;

                if (this.x > width + 100 || this.y > height + 100 || this.opacity <= 0) {
                    this.active = false;
                }
            }

            draw(ctx: CanvasRenderingContext2D) {
                if (!this.active) return;

                const tailX = this.x - Math.cos(this.angle) * this.length;
                const tailY = this.y - Math.sin(this.angle) * this.length;

                const gradient = ctx.createLinearGradient(this.x, this.y, tailX, tailY);
                gradient.addColorStop(0, `rgba(255, 255, 255, ${this.opacity})`);
                gradient.addColorStop(1, `rgba(100, 200, 255, 0)`); // Blueish comet tail

                ctx.lineWidth = 2;
                ctx.lineCap = 'round';
                ctx.strokeStyle = gradient;
                ctx.beginPath();
                ctx.moveTo(this.x, this.y);
                ctx.lineTo(tailX, tailY);
                ctx.stroke();

                // Bright head
                ctx.fillStyle = `rgba(255, 255, 255, ${this.opacity})`;
                ctx.beginPath();
                ctx.arc(this.x, this.y, 1.5, 0, Math.PI * 2);
                ctx.fill();

                // Glow
                ctx.shadowBlur = 10;
                ctx.shadowColor = "white";
                ctx.stroke();
                ctx.shadowBlur = 0;
            }
        }

        const resizeCanvas = () => {
            canvas.width = window.innerWidth;
            canvas.height = window.innerHeight;
            initStars();
        };

        const initStars = () => {
            stars = [];
            shootingStars = [];
            for (let i = 0; i < starCount; i++) {
                stars.push(new Star(canvas.width, canvas.height, true));
            }
            // Create a pool of shooting stars
            for (let i = 0; i < 3; i++) {
                shootingStars.push(new ShootingStar(canvas.width, canvas.height));
            }
        };

        const animate = () => {
            // Draw background
            const gradient = ctx.createLinearGradient(0, 0, 0, canvas.height);
            gradient.addColorStop(0, '#02010a');
            gradient.addColorStop(1, '#0b041c');
            ctx.fillStyle = gradient;
            ctx.fillRect(0, 0, canvas.width, canvas.height);

            // Draw Stars
            stars.forEach(star => {
                star.update();
                star.draw(ctx);
            });

            // Draw Shooting Stars
            shootingStars.forEach(star => {
                star.update(canvas.width, canvas.height);
                star.draw(ctx);
            });

            animationFrameId = requestAnimationFrame(animate);
        };

        window.addEventListener('resize', resizeCanvas);
        resizeCanvas();
        animate();

        return () => {
            window.removeEventListener('resize', resizeCanvas);
            cancelAnimationFrame(animationFrameId);
        };
    }, []);

    return (
        <canvas
            ref={canvasRef}
            className="fixed inset-0 pointer-events-none"
            style={{ zIndex: -15 }}
        />
    );
};

export default StarField;
