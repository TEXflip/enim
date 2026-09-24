void main_loop(WindowData window_data) {
    while (!RGFW_window_shouldClose(window_data.win)) {
        RGFW_pollEvents();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        glUseProgram(window_data.main_program);
        glBindVertexArray(window_data.vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        RGFW_window_swapBuffers_OpenGL(window_data.win);
        glFlush();
    }
}