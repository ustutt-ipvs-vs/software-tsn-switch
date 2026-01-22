# How to change the pipeline Docker container

1. Modify the `Dockerfile` and build the image locally to test it.
    ```
    sudo docker build -t <image_name> .
    ```
2. To run builds and tests in on your test container, use this command from the repo's root to mount it to the container.
   The repo will then be located in `/root/enpro-switch/enpro-switch/`
   ```
   sudo docker run -it -v .:/root/enpro-switch/enpro-switch <image_name> /bin/bash
   ```
3. If everything is in order, log into the Gitlab container registry (use your Gitlab credentials)
    ```
    sudo docker login gitlab-vs.informatik.uni-stuttgart.de:5050
    ```
4. Check the version numbers in the [container registry](https://gitlab-vs.informatik.uni-stuttgart.de/software-tsn-switch/enpro-switch/container_registry/42).
   The new version number should be one higher than the currently highest number (e.g., 003 -> 004)
5. Build the Docker image for the registry.
   Replace `<version>` with the new version number, e.g. `004`.
   Your docker engine should be able to just use the cached layers from your test build without needing to rebuild.
   ```
   sudo docker build -t gitlab-vs.informatik.uni-stuttgart.de:5050/software-tsn-switch/enpro-switch/test-pipeline:<version> .
   ```
6. Push the image.
   Again, replace `<version>` with the new version number.
   ```
   sudo docker push gitlab-vs.informatik.uni-stuttgart.de:5050/software-tsn-switch/enpro-switch/test-pipeline:<version>
   ```
7. Now update this line in `.gitlab-ci.yml`, replacing `<version>` with the new version number:
   ```
   image: gitlab-vs.informatik.uni-stuttgart.de:5050/software-tsn-switch/enpro-switch/test-pipeline:<version>
   ```
8. Commit and push the changes to both `Dockerfile` and `.gitlab-ci.yml`!
   If you like conventional commit messages, use `ci: <message>`
9. The pipeline that runs after you push should already use the new image!
