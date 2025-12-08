// @ts-check
import { defineConfig, passthroughImageService } from 'astro/config';
import cloudflare from '@astrojs/cloudflare';

// https://astro.build/config
export default defineConfig({
  output: 'server',
  adapter: cloudflare({
    platformProxy: {
      enabled: true,
    },
  }),
  image: {
    service: passthroughImageService(),
  },
  vite: {
    server: {
      proxy: {
        // Proxy API calls to production during dev (use with: bun run dev:prod)
        ...(process.env.USE_PROD_API === '1' ? {
          '/api': {
            target: 'https://epd-sensor-dashboard.pages.dev',
            changeOrigin: true,
          },
        } : {}),
      },
    },
  },
});
