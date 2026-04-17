import { defineConfig } from 'vite';

export default defineConfig({
    optimizeDeps: {
        include: ['google-protobuf', 'grpc-web']
    },
    build: {
        commonjsOptions: {
            include: [/google-protobuf/, /grpc-web/, /node_modules/]
        }
    }
});